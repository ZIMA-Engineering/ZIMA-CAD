"""Verify the approved Feature Up To contract on isolated committed factory data.

The native tool performs fresh calculation, save and readback. This driver
checks the unchanged authored identities, source packets and physical values.
It never installs outputs, stages Git changes or overwrites working libraries.
"""
import argparse
import base64
import concurrent.futures
import configparser
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
NATIVE = {'.prtz', '.asmz', '.frmz', '.tblz', '.symz'}
CORRECTIONS = {'parameter:side' + str(side) + '_up_to_' + quantity + '_correction'
               for side in range(2) for quantity in ('length', 'angle')}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ini(path):
    value = configparser.ConfigParser(interpolation=None, strict=False)
    value.optionxform = str
    value.read(path, encoding='utf-8-sig')
    return value


def feature_owners(data):
    return {data[section]['param.feature_parent_id'] for section in data.sections()
            if section.startswith('Entity.') and data[section].get('kind') == 'feature'}


def identifiers(data):
    registry = json.loads(data['Document']['dimension_identifiers'])
    return {(row['owner'], row['key']): row['number'] for row in registry['entries']}


def cache(data):
    if not data.has_section('CachedBodies'):
        return []
    packet = json.loads(data['CachedBodies']['data'])
    if isinstance(packet, list):
        return packet
    assert packet['format'] == 'zima-shared-cache-v1'
    if 'compression' in packet:
        assert packet['compression'] == 'zlib-base64'
        decoder = zlib.decompressobj()
        raw = decoder.decompress(base64.b64decode(packet['data'], validate=True)) + decoder.flush()
        assert decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail
        assert len(raw) == packet['uncompressed_bytes']
        packet = json.loads(raw)
        assert packet['format'] == 'zima-shared-cache-v1' and 'compression' not in packet
    values, memo = packet['values'], {}

    def expand(value):
        if isinstance(value, dict):
            if set(value) == {'$zima_cache'}:
                index = value['$zima_cache']
                if index not in memo:
                    memo[index] = expand(values[index])
                return memo[index]
            return {key: expand(item) for key, item in value.items()}
        if isinstance(value, list):
            return [expand(item) for item in value]
        return value

    bodies = []
    for body in expand(packet['root']):
        if body.get('body_source') == 'previous_boundary':
            body = bodies[-1] | body
        bodies.append(body)
    return bodies


def identities(value):
    found, visited = set(), set()

    def walk(item):
        if isinstance(item, (dict, list)):
            if id(item) in visited:
                return
            visited.add(id(item))
        if isinstance(item, dict):
            if 'owner' in item and 'key' in item:
                found.add((item['owner'], item['key'], item.get('instance_path', '')))
            for child in item.values():
                walk(child)
        elif isinstance(item, list):
            for child in item:
                walk(child)
    walk(value)
    return found


def peak_memory(process):
    if sys.platform != 'win32':
        return None
    import ctypes
    from ctypes import wintypes

    class Counters(ctypes.Structure):
        _fields_ = [('cb', wintypes.DWORD), ('faults', wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in ('peak', 'working', 'peak_paged',
            'paged', 'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile', 'private')]
    counters = Counters()
    counters.cb = ctypes.sizeof(counters)
    query = ctypes.windll.psapi.GetProcessMemoryInfo
    query.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
    return counters.peak if query(int(process._handle), ctypes.byref(counters), counters.cb) else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, default=ROOT.parents[2] / 'zc-up-to-20261023')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/feature-up-to-factory')
    parser.add_argument('--native', type=Path, default=ROOT / 'build/cpp-windows-release/zima_refresh_start_templates.exe')
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--limit', type=int)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--resume', action='store_true')
    args = parser.parse_args()
    stage, output = args.stage.resolve(), args.output.resolve()
    source, verified = stage / 'source', stage / 'verified'
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT).decode().strip()
    marker = stage / '.zima-up-to-factory.json'
    ownership = {'repository': str(ROOT), 'commit': commit, 'kind': 'feature-up-to-factory'}
    if marker.exists():
        assert json.loads(marker.read_text(encoding='utf-8')) == ownership
    else:
        assert not stage.exists() or not any(stage.iterdir()), 'Refusing an unrelated stage'
        stage.mkdir(parents=True, exist_ok=True)
        marker.write_text(json.dumps(ownership, indent=2), encoding='utf-8')
    output.mkdir(parents=True, exist_ok=True)
    refreshed_templates = {}
    template_receipt = output / 'templates-verification.json'
    if template_receipt.exists():
        receipt = json.loads(template_receipt.read_text(encoding='utf-8'))
        assert receipt['ok'] and receipt['commit'] == commit
        refreshed_templates = {row['path']: row['sha256'] for row in receipt['assets']}
    names = subprocess.check_output(['git', 'ls-files', '-z', 'config'], cwd=ROOT).decode('utf-8').split('\0')
    names = sorted(name for name in names if Path(name).suffix.lower() in NATIVE | {'.drwz'})
    items = []
    reader = subprocess.Popen(['git', 'cat-file', '--batch'], cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for name in names:
            assert not Path(name).is_absolute() and '..' not in Path(name).parts
            reader.stdin.write((commit + ':' + name + '\n').encode('utf-8'))
            reader.stdin.flush()
            header = reader.stdout.readline().decode().split()
            assert len(header) == 3 and header[1] == 'blob', header
            blob = reader.stdout.read(int(header[2]))
            assert len(blob) == int(header[2]) and reader.stdout.read(1) == b'\n'
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if path.exists():
                assert path.read_bytes() == blob, 'Changed isolated committed source: ' + name
            else:
                path.write_bytes(blob)
            owners = feature_owners(ini(path)) if path.suffix.lower() in NATIVE - {'.asmz'} else set()
            affected = bool(owners)
            items.append({'path': name, 'affected': affected, 'feature_owners': sorted(owners),
                          'source_sha256': hashlib.sha256(blob).hexdigest(), 'source_bytes': len(blob),
                          'working_sha256': sha(ROOT / name)})
            if not affected:
                target = verified / name
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.exists():
                    template_refresh = name.startswith('config/templates/') and refreshed_templates.get(name) == sha(target)
                    assert target.read_bytes() == blob or template_refresh, 'Changed unconverted factory output: ' + name
                else:
                    target.write_bytes(blob)
    finally:
        reader.stdin.close()
        assert reader.wait() == 0
    affected = [item for item in items if item['affected']]
    forms = [item['path'] for item in affected if '/01-FORM/' in item['path']]
    assert len(affected) == 1016 and len(forms) == 4, (len(affected), forms)
    assert len(items) == 1185 and sum(Path(item['path']).suffix == '.drwz' for item in items) == 137
    inventory = {'commit': commit, 'stage': str(stage), 'assets': items, 'forms': forms}
    (output / 'inventory.json').write_text(json.dumps(inventory, indent=2), encoding='utf-8')
    print('Prepared committed sources:', len(items), '; affected Parts:', len(affected), '; FORM:', len(forms), flush=True)
    if args.prepare_only:
        return
    native = args.native.resolve()
    runtime_hash = sha(native)
    report_path = output / 'verification.json'
    previous = {}
    if args.resume and report_path.exists():
        report = json.loads(report_path.read_text(encoding='utf-8'))
        assert report['commit'] == commit and report['native_sha256'] == runtime_hash, 'Verification runtime changed'
        previous = {row['path']: row for row in report['assets'] if row['ok']}
    selected = affected[:args.limit] if args.limit else affected
    reports = output / 'assets'
    reports.mkdir(exist_ok=True)
    environment = os.environ | {'QT_QPA_PLATFORM': 'offscreen', 'QT_STYLE_OVERRIDE': 'Fusion',
                               'TEMP': str(ROOT / 'build/pre-release-test-temp'), 'TMP': str(ROOT / 'build/pre-release-test-temp')}

    def convert(item):
        name = item['path']
        target, original = verified / name, source / name
        prior = previous.get(name)
        if prior and target.exists() and sha(target) == prior['output_sha256']:
            assert sha(ROOT / name) == item['working_sha256']
            return prior
        start = time.monotonic()
        evidence_path = reports / (hashlib.sha256(name.encode()).hexdigest() + '.json')
        try:
            process = subprocess.Popen([str(native), '--regenerate-asset', str(original), str(target), str(evidence_path)],
                cwd=source, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                stdout, stderr = process.communicate(timeout=300)
            except subprocess.TimeoutExpired:
                process.kill()
                stdout, stderr = process.communicate()
                raise RuntimeError('Native asset verification exceeded 300 seconds')
            memory = peak_memory(process)
            assert process.returncode == 0, (stdout + stderr).decode('utf-8', errors='replace')[-4000:]
            evidence = json.loads(evidence_path.read_text(encoding='utf-8'))
            assert all(evidence[key] for key in ('native_readback_exact', 'authored_history_unchanged', 'sketches_unchanged'))
            before, after = ini(original), ini(target)
            old_ids, new_ids = identifiers(before), identifiers(after)
            assert all(new_ids.get(key) == number for key, number in old_ids.items()), 'Changed existing dimension identity'
            extra = new_ids.keys() - old_ids.keys()
            assert all(owner in item['feature_owners'] and key in CORRECTIONS for owner, key in extra), 'Unexpected dimension slots'
            assert len(set(new_ids.values())) == len(new_ids), 'Duplicate dimension number'
            old_cache, new_cache = cache(before), cache(after)
            assert len(old_cache) == len(new_cache), 'Changed boundary count'
            for index, (old, new) in enumerate(zip(old_cache, new_cache)):
                assert old['original_references'] == new['original_references'], ('Changed original source packet', index)
                assert identities(old) <= identities(new), ('Lost reference or side identity', index)
                for kind in ('faces', 'edges', 'vertices'):
                    assert identities(old.get('kernel_bindings', {}).get(kind, [])) <= identities(new.get('kernel_bindings', {}).get(kind, []))
                for key in ('volume', 'surface_area'):
                    a, b = old.get(key), new.get(key)
                    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
                        assert math.isclose(a, b, rel_tol=1e-8, abs_tol=1e-6), (index, key, a, b)
                    else:
                        # Intermediate analysis can intentionally be deferred.
                        # Pending markers are neither measured values nor zero.
                        deferred = isinstance(old.get('volume'), dict) and old['volume'].get('pending') is True
                        pending_value = isinstance(a, dict) and a.get('pending') is True
                        assert a == b and (pending_value or (key == 'surface_area' and a is None and deferred)), (index, key, a, b)
            assert not evidence.get('errors') and evidence.get('physical_properties_current'), 'Final physical analysis is unavailable'
            for section in ('UserParameters', 'UserParameterLabels', 'UserParameterValues', 'DocumentUnits', 'DocumentPrecision',
                            'Material', 'MaterialDescriptions', 'MaterialProperties', 'MaterialUnits', 'Relations'):
                assert dict(before[section]) == dict(after[section]), ('Changed authored metadata', section)
            assert sha(original) == item['source_sha256'] and sha(ROOT / name) == item['working_sha256'], 'Changed source or private working file'
            return {'path': name, 'ok': True, 'seconds': time.monotonic() - start, 'peak_working_set_bytes': memory,
                    'source_bytes': item['source_bytes'], 'output_bytes': target.stat().st_size, 'output_sha256': sha(target),
                    'new_dimension_slots': len(extra), 'evidence': evidence}
        except Exception as error:
            return {'path': name, 'ok': False, 'seconds': time.monotonic() - start, 'error': str(error)}

    rows, started = [], time.monotonic()

    def publish():
        report = {'commit': commit, 'native_sha256': runtime_hash, 'stage': str(stage), 'partial': len(selected) != len(affected),
                  'total': len(rows), 'passed': sum(row['ok'] for row in rows), 'seconds': time.monotonic() - started, 'assets': rows}
        report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for row in pool.map(convert, selected):
            rows.append(row)
            if not row['ok']:
                print('FAILED', row['path'], row['error'], flush=True)
            if len(rows) % 25 == 0:
                print('Verified', len(rows), '/', len(selected), flush=True)
                publish()
    publish()
    print('Verified', sum(row['ok'] for row in rows), '/', len(rows), 'affected native assets', flush=True)
    if not all(row['ok'] for row in rows):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
