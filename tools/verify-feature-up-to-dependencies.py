"""Check isolated factory dependencies after the approved Feature conversion.

Requires a complete successful verify-feature-up-to-factory.py report. Original
working libraries remain read-only. Native loads and Drawing recalculation run
in separate settings directories and retain their JSON evidence.
"""
import argparse
import concurrent.futures
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
HELPERS = runpy.run_path(str(ROOT / 'tools/verify-feature-up-to-factory.py'))
sha, ini = HELPERS['sha'], HELPERS['ini']


def definition(path):
    return json.loads(ini(path)['Document']['param.cpp_drawing'])


def portable_sheets(sheets):
    result = copy.deepcopy(sheets)
    for sheet in result:
        for row in sheet.get('bom_rows', []):
            path = row['source_path']
            if row['designation'] == row['source_document_id'] + '|' + path:
                row['designation'] = row['source_document_id'] + '|' + Path(path).name
            row['source_path'] = Path(path).name
        for view in sheet['views']:
            # Shared display-pool addresses do not identify source geometry.
            view.pop('output_source', None)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('load', 'drawings', 'templates', 'mass'))
    parser.add_argument('--evidence', type=Path, default=ROOT / 'build/feature-up-to-factory')
    parser.add_argument('--cli', type=Path, default=ROOT / 'build/cpp-windows-release/zima-cad-cli.exe')
    parser.add_argument('--template-tool', type=Path, default=ROOT / 'build/cpp-windows-release/zima_refresh_start_templates.exe')
    parser.add_argument('--baseline-cli', type=Path)
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--limit', type=int)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    inventory = json.loads((evidence / 'inventory.json').read_text(encoding='utf-8'))
    converted = json.loads((evidence / 'verification.json').read_text(encoding='utf-8'))
    assert not converted['partial'] and converted['total'] == converted['passed'] == 1016
    assert inventory['commit'] == converted['commit']
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip() == inventory['commit']
    stage = Path(inventory['stage'])
    ownership = json.loads((stage / '.zima-up-to-factory.json').read_text(encoding='utf-8'))
    assert ownership == {'repository': str(ROOT), 'commit': inventory['commit'], 'kind': 'feature-up-to-factory'}
    source, verified = stage / 'source', stage / 'verified'
    for row in converted['assets']:
        assert sha(verified / row['path']) == row['output_sha256'], 'Changed verified Part: ' + row['path']
    environment = os.environ | {'QT_QPA_PLATFORM': 'offscreen', 'QT_STYLE_OVERRIDE': 'Fusion',
        'ZIMA_CPP_CALC_PROFILE': '1', 'TEMP': str(ROOT / 'build/pre-release-test-temp'),
        'TMP': str(ROOT / 'build/pre-release-test-temp')}
    cli = args.cli.resolve()

    if args.mode == 'mass':
        rows = []
        for item in converted['assets']:
            data = ini(verified / item['path'])
            measured = item['evidence']
            volume = measured['volume']
            if volume == 0:
                rows.append({'path': item['path'], 'ok': True, 'kind': 'non-solid/reference-only', 'volume': volume})
                continue
            assert volume > 0 and measured['physical_properties_current']
            assert data['MaterialUnits']['MASS_DENSITY'] == 'kg/mm^3', 'Unsupported density units: ' + item['path']
            assert data['DocumentUnits']['Length'] == 'mm', 'Unsupported native length units: ' + item['path']
            density = float(data['MaterialProperties']['MASS_DENSITY'])
            published = float(measured['parameters']['mass'][''])
            expected = volume * density
            places = int(data['DocumentPrecision']['decimal_places'])
            assert 0 <= places <= 15
            publication_rounding = .5 * 10 ** -places
            assert density > 0 and expected > 0 and published > 0, 'Unavailable or stale mass: ' + item['path']
            assert math.isclose(published, expected, rel_tol=1e-12, abs_tol=publication_rounding + 1e-12), (item['path'], published, expected)
            rows.append({'path': item['path'], 'ok': True, 'kind': 'solid', 'volume': volume,
                         'density_kg_per_mm3': density, 'published_mass_kg': published, 'expected_mass_kg': expected,
                         'authored_decimal_places': places})
        (evidence / 'mass-verification.json').write_text(json.dumps({'commit': inventory['commit'],
            'total': len(rows), 'passed': len(rows), 'assets': rows}, indent=2), encoding='utf-8')
        print('Verified density and published mass:', sum(row['kind'] == 'solid' for row in rows),
              '; intentional non-solid/reference-only:', sum(row['kind'] != 'solid' for row in rows))
        return

    def commands(runtime, folder, entries):
        payload = ''.join(json.dumps({'command': command, 'arguments': values}) + '\n' for command, values in entries)
        result = subprocess.run([str(runtime), '--working-directory', str(folder), '--stdin'],
            cwd=verified, env=environment, input=payload.encode(), capture_output=True, timeout=240)
        (folder / 'stdout.jsonl').write_bytes(result.stdout)
        (folder / 'stderr.log').write_bytes(result.stderr)
        records = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
        assert result.returncode == 0 and len(records) == len(entries) and all(row['ok'] for row in records), (
            result.returncode, records, result.stderr[-2000:])
        assert b'Kernel operation count=' not in result.stderr, 'Dependency inspection recalculated a source Part'
        return records

    if args.mode == 'templates':
        tool = args.template_tool.resolve()
        assert sha(tool) == converted['native_sha256'], 'Template runtime differs from the conversion runtime'
        names = ('START_PART_mm.prtz', 'START_PART_in.prtz', 'START_SKELETON_mm.prtz',
                 'START_SKELETON_in.prtz', 'START_ASSEMBLY_mm.asmz', 'START_ASSEMBLY_in.asmz')
        result = subprocess.run([str(tool)], cwd=verified, env=environment, capture_output=True, timeout=60)
        assert result.returncode == 0, result.stdout + result.stderr
        rows = [{'path': 'config/templates/' + name, 'sha256': sha(verified / 'config/templates' / name)} for name in names]
        report = {'commit': inventory['commit'], 'native_sha256': sha(tool), 'ok': True,
                  'assets': rows, 'output': result.stdout.decode()}
        (evidence / 'templates-verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print('Six isolated templates calculated, saved and reopened; GUI new-document checks remain required')
        return

    selected = inventory['assets']
    if args.mode == 'drawings':
        assert args.baseline_cli and args.baseline_cli.is_file(), 'Drawing checks require an accepted baseline CLI'
        selected = [row for row in selected if Path(row['path']).suffix == '.drwz']
        assert len(selected) == 137
    if args.limit:
        selected = selected[:args.limit]
    folder_root = evidence / (args.mode + '-checks')
    folder_root.mkdir(exist_ok=True)

    def verify(item):
        start = time.monotonic()
        name = item['path']
        folder = folder_root / hashlib.sha256(name.encode()).hexdigest()[:20]
        folder.mkdir(exist_ok=True)
        target = verified / name
        before = sha(target)
        try:
            if args.mode == 'load':
                path = target
                if target.suffix in {'.symz', '.frmz', '.tblz'}:
                    path = folder / 'library.prtz'
                    shutil.copyfile(target, path)
                records = commands(cli, folder, [('open', {'path': str(path)}), ('close', {'discard': True})])
                detail = {'native_open': records[0]['ok']}
            else:
                authored = definition(source / name)
                relative_part = Path(authored['source_path'])
                assert not relative_part.is_absolute() and '..' not in relative_part.parts
                source_part = (target.parent / relative_part).resolve()
                assert source_part.is_relative_to(verified.resolve()) and source_part.is_file()
                current = folder / 'current'
                baseline = folder / 'baseline'
                for destination, part in ((current, source_part), (baseline, (source / name).parent / relative_part)):
                    destination.mkdir(exist_ok=True)
                    shutil.copyfile(source / name, destination / Path(name).name)
                    copy_part = destination / relative_part
                    copy_part.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(part, copy_part)
                current_part = current / relative_part
                part_hash = sha(current_part)
                current_drawing = current / Path(name).name
                entries = [('open', {'path': str(current_drawing)}), ('regenerate', {}),
                    ('drawing.dimension.list', {}), ('drawing.annotation.list', {'kind': 'dimension', 'mode': 'erase'}),
                    ('save', {}), ('close', {'discard': True}), ('open', {'path': str(current_drawing)}),
                    ('drawing.dimension.list', {}), ('drawing.annotation.list', {'kind': 'dimension', 'mode': 'erase'}),
                    ('close', {'discard': True})]
                records = commands(cli, current, entries)
                for index in (2, 3):
                    old, new = records[index]['data']['items'], records[index + 5]['data']['items']
                    assert old == new, 'Native reopen changed dimension state'
                    if index == 2:
                        assert all(row['state'] == 'resolved' for row in old), 'Unresolved Drawing dimension'
                    else:
                        assert all(not row.get('unresolved') for row in old), 'Unresolved model annotation'
                after = definition(current_drawing)
                for key in ('source_document_id', 'source_path', 'document_id', 'dimension_identifiers'):
                    assert authored[key] == after[key], 'Changed authored Drawing identity: ' + key
                baseline_drawing = baseline / Path(name).name
                commands(args.baseline_cli.resolve(), baseline, [('open', {'path': str(baseline_drawing)}),
                    ('regenerate', {}), ('save', {}), ('close', {'discard': True})])
                assert portable_sheets(definition(baseline_drawing)['sheets']) == portable_sheets(after['sheets']), 'Drawing differs from the accepted native baseline'
                assert sha(current_part) == part_hash, 'Drawing refresh rewrote its source Part'
                detail = {'dimensions': len(records[2]['data']['items']), 'annotations': len(records[3]['data']['items']),
                    'reopen_exact': True, 'baseline_sheets_equal': True, 'source_part_calculations': 0}
            assert sha(target) == before and sha(ROOT / name) == item['working_sha256'], 'Changed factory or private working file'
            return {'path': name, 'ok': True, 'sha256': before, 'seconds': time.monotonic() - start, **detail}
        except Exception as error:
            return {'path': name, 'ok': False, 'seconds': time.monotonic() - start, 'error': str(error)}

    rows, started = [], time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for row in pool.map(verify, selected):
            rows.append(row)
            if not row['ok']:
                print('FAILED', row['path'], row['error'], flush=True)
            if len(rows) % 50 == 0:
                print('Checked', len(rows), '/', len(selected), flush=True)
    report = {'commit': inventory['commit'], 'cli_sha256': sha(cli), 'partial': bool(args.limit),
              'total': len(rows), 'passed': sum(row['ok'] for row in rows), 'seconds': time.monotonic() - started, 'assets': rows}
    if args.baseline_cli:
        report['baseline_cli_sha256'] = sha(args.baseline_cli)
    (evidence / (args.mode + '-verification.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(args.mode, report['passed'], '/', report['total'], flush=True)
    if report['passed'] != report['total']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
