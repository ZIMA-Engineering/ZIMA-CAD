"""Stage the approved Sketch 35 -> 36 authoring conversion, then verify natively.

This is an offline, single-use authoring tool, never a product legacy reader.
Sources and private working libraries remain unchanged. Native calculation,
save/reopen, authored identity and physical/reference checks are mandatory.
"""
import argparse
import base64
import concurrent.futures
import configparser
import hashlib
import json
import math
import os
import shutil
from pathlib import Path
import subprocess
import struct
import time

from importlib.util import spec_from_file_location, module_from_spec

ROOT = Path(__file__).resolve().parents[1]
_spec = spec_from_file_location('factory_checks', ROOT / 'tools/verify-feature-up-to-factory.py')
checks = module_from_spec(_spec)
_spec.loader.exec_module(checks)
EXTENSIONS = {'.prtz', '.asmz', '.drwz', '.frmz', '.tblz', '.symz'}


def dump(value):
    return json.dumps(value, ensure_ascii=False, separators=(',', ':'))


def flip_style(style):
    if style.get('tolerance_mode') == 'deviations':
        style['upper_tolerance'], style['lower_tolerance'] = style.get('lower_tolerance', ''), style.get('upper_tolerance', '')
    elif style.get('tolerance_mode') == 'single_deviation' and style.get('single_tolerance'):
        text = style['single_tolerance']
        # Native annotation tolerances are numeric strings. Preserve precision.
        float(text)
        style['single_tolerance'] = text[1:] if text.startswith('-') else '-' + text.lstrip('+')


def coordinate(d):
    axis = d.get('geometry') in {'sketch_axis:x', 'sketch_axis:y'}
    return (d.get('kind') in {'distance_x', 'distance_y'} and
            (axis or d.get('first') == 'sketch_origin' or d.get('second') == 'sketch_origin')) or (
            d.get('kind') == 'distance_point_line' and axis)


def same_reference_geometry(before, after):
    if before == after:
        return True
    # OCCT can permute tessellation vertices/triangles during fresh evaluation.
    # Compare exact doubles and oriented triangles with their exact face owner;
    # no tolerance, rounding or reduced geometry precision is used here.
    binary = {'vertices_binary', 'triangles_binary'}
    if {k: v for k, v in before.items() if k not in binary} != {k: v for k, v in after.items() if k not in binary}:
        return canonical_reference_geometry(before)==canonical_reference_geometry(after)
    def canonical(packet):
        vertices = list(struct.iter_unpack('<ddd', base64.b64decode(packet['vertices_binary'])))
        owners = []
        for owner, count in struct.iter_unpack('<II', base64.b64decode(packet['triangle_reference_runs_binary'])):
            owners.extend([owner] * count)
        triangles = list(struct.iter_unpack('<III', base64.b64decode(packet['triangles_binary'])))
        assert len(owners) == len(triangles)
        faces = sorted((owner, min(tuple(vertices[i] for i in triangle[k:] + triangle[:k]) for k in range(3)))
                       for owner, triangle in zip(owners, triangles))
        return sorted(set(vertices)), faces
    return canonical(before) == canonical(after)


def canonical_reference_geometry(packet):
    """Exact geometry, orientation, side metadata and identities; index-free.

    Reference-table, edge and point enumeration is disposable serialization
    order. Each primitive is paired with its real full reference record. Curve
    direction and triangle winding are retained. Repeated indices at exactly the
    same vertex are interchangeable; each oriented triangle still keeps all three
    positions and its face reference. No floating-point rounding is
    performed, including for signed zero.
    """
    encode=lambda value:json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':'))
    vectors=lambda key:list(struct.iter_unpack('<ddd',base64.b64decode(packet[key])))
    indices=lambda key:[i[0] for i in struct.iter_unpack('<I',base64.b64decode(packet[key]))]
    references=packet['references'];vertices=vectors('vertices_binary');owners=[]
    for owner,count in struct.iter_unpack('<II',base64.b64decode(packet['triangle_reference_runs_binary'])):
        owners.extend([owner]*count)
    triangles=list(struct.iter_unpack('<III',base64.b64decode(packet['triangles_binary'])))
    assert len(owners)==len(triangles)
    faces=sorted(encode([references[owner],min(tuple(vertices[i] for i in tri[k:]+tri[:k]) for k in range(3))])
                 for owner,tri in zip(owners,triangles))
    edge_points=vectors('edge_points_binary');offsets=indices('edge_offsets_binary');edge_refs=indices('edge_references_binary')
    assert len(offsets)==len(edge_refs)+1 and offsets[-1]==len(edge_points)
    edges=sorted(encode([references[ref],edge_points[offsets[i]:offsets[i+1]],packet['edge_lengths'][i],
                        packet['edge_splines'][i],packet['edge_sheets'][i],packet['edge_surfaces'][i]]) for i,ref in enumerate(edge_refs))
    positions=vectors('point_positions_binary');point_refs=indices('point_references_binary')
    assert len(positions)==len(point_refs)
    points=sorted(encode([references[ref],position,packet['point_surfaces'][i]]) for i,(ref,position) in enumerate(zip(point_refs,positions)))
    axes=sorted(encode(axis|{'reference':references[axis['reference']]}) for axis in packet['axes'])
    return packet['encoding'],sorted({encode(p) for p in vertices}),sorted(map(encode,references)),faces,edges,points,axes


def prepare_file(source, output):
    """Seed current-contract input; native authoring then rewrites expressions.

    Canonical Sketch geometry, values and independent branch flags stay exact.
    Limits/tolerances must be re-expressed before the strict current reader.
    """
    flipped, sketches, overrides = set(), [], []

    def walk(value):
        if isinstance(value, str) and value.lstrip().startswith(('{', '[')):
            try:
                parsed = json.loads(value)
            except (ValueError, TypeError):
                return value
            changed = walk(parsed)
            return dump(changed) if changed != parsed else value
        if isinstance(value, list):
            return [walk(item) for item in value]
        if not isinstance(value, dict):
            return value
        result = {key: walk(item) for key, item in value.items()}
        if 'calculated_boundaries' in result and 'calculation_definition_fingerprint' in result:
            result['calculated_boundaries'] = []
            result['calculation_definition_fingerprint'] = ''
        if value.get('format') == 'zima-cad-cpp-sketch':
            assert value['version'] == 35, 'Input must be the exact unconverted Sketch contract'
            result['version'] = 36
            sketches.append(value['id'])
            if value['plane'] == 'xz':
                for d in result['dimensions']:
                    if not coordinate(d) or not (d['kind'] == 'distance_y' or
                            d['kind'] == 'distance_point_line' and d['geometry'] == 'sketch_axis:x'):
                        continue
                    flipped.add((value['id'], 'dimension:' + d['id']))
                    lower, upper = d.pop('lower_limit', None), d.pop('upper_limit', None)
                    if upper is not None:
                        d['lower_limit'] = -upper
                    if lower is not None:
                        d['upper_limit'] = -lower
                    flip_style(d)
                    if d.get('display_text_override'):
                        overrides.append(d['id'])
        return result

    # Keep unrelated native sections and cached BRep/source packets byte exact.
    lines, changed, section = [], 0, ''
    raw = source.read_text(encoding='utf-8-sig')
    fixture = walk(json.loads(raw)) if source.suffix == '.json' else None
    for line in ([] if fixture is not None else raw.splitlines(keepends=True)):
        if line.startswith('['):
            section = line.strip()[1:-1]
        if section == 'CachedBodies':
            # An authored contract changes calculation fingerprints. Fresh
            # native evaluation below replaces this disposable derived cache.
            continue
        if '=' not in line:
            lines.append(line)
            continue
        key, text = line.split('=', 1)
        if not text.lstrip().startswith(('{', '[', '"{', '"[')):
            lines.append(line)
            continue
        try:
            old = json.loads(text)
        except ValueError:
            lines.append(line)
            continue
        # Geometry caches contain no authored Sketches and can be very large.
        new = old if isinstance(old, dict) and old.get('format') == 'zima-shared-cache-v1' else walk(old)
        if new != old:
            changed += 1
            lines.append(key + '=' + dump(new) + '\n')
        else:
            lines.append(line)

    # Global and embedded Sketch annotation overrides use the same named value.
    def layouts(value):
        if isinstance(value, str) and value.lstrip().startswith(('{', '[')):
            try:
                old = json.loads(value)
            except ValueError:
                return value
            new = layouts(old)
            return dump(new) if new != old else value
        if isinstance(value, list):
            return [layouts(item) for item in value]
        if not isinstance(value, dict):
            return value
        result = {k: layouts(v) for k, v in value.items()}
        owner, key = value.get('owner_id', value.get('owner')), value.get('semantic_key', value.get('key'))
        if (owner, key) in flipped:
            style = result.get('layout', result).get('text_style')
            if style:
                flip_style(style)
        return result

    if fixture is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(layouts(fixture),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        return {'sketches': len(sketches), 'flipped': len(flipped), 'overrides': overrides, 'changed_fields': 1}
    final, section = [], ''
    for line in lines:
        if line.startswith('['):
            section = line.strip()[1:-1]
        if '=' in line and section != 'CachedBodies':
            key, text = line.split('=', 1)
            if text.lstrip().startswith(('{', '[', '"{', '"[')):
                try:
                    old = json.loads(text)
                    new = layouts(old) if not (isinstance(old, dict) and old.get('format') == 'zima-shared-cache-v1') else old
                    if new != old:
                        line = key + '=' + dump(new) + '\n'
                except ValueError:
                    pass
        final.append(line)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(''.join(final), encoding='utf-8', newline='')
    return {'sketches': len(sketches), 'flipped': len(flipped), 'overrides': overrides, 'changed_fields': changed}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, default=ROOT / 'build/coordinate-factory')
    parser.add_argument('--native', type=Path, default=ROOT / 'build/cpp-coordinate-release/zima_refresh_start_templates.exe')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--limit', type=int)
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--factory-only', action='store_true')
    args = parser.parse_args()
    stage, native = args.stage.resolve(), args.native.resolve()
    assert shutil.disk_usage(stage.parent).free>=3*1024**3,'Need at least 3 GiB free before isolated verification'
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    ownership = {'repository': str(ROOT), 'commit': commit, 'kind': 'coordinate-contract-36'}
    marker = stage / 'ownership.json'
    if marker.exists():
        assert json.loads(marker.read_text(encoding='utf-8')) == ownership, 'Unrelated or obsolete stage'
    else:
        assert not stage.exists() or not any(stage.iterdir()), 'Refusing an unrelated directory'
        stage.mkdir(parents=True, exist_ok=True)
        marker.write_text(dump(ownership), encoding='utf-8')
    names = subprocess.check_output(['git', 'ls-files', '-z', 'config', 'cpp/tests/fixtures', 'tests/fixtures/drawing-library'], cwd=ROOT).decode().split('\0')
    names = sorted(name for name in names if Path(name).suffix.lower() in EXTENSIONS or
                   not name.startswith('config/') and Path(name).suffix.lower()=='.json')
    # Other fixture formats intentionally exercise unsupported input. Convert
    # only fixtures authored with the currently replaced native Sketch contract.
    names = [name for name in names if name.startswith('config/') or b'zima-cad-cpp-sketch' in (ROOT / name).read_bytes()]
    if args.factory_only:
        names=[name for name in names if name.startswith('config/')]
    reader = subprocess.Popen(['git', 'cat-file', '--batch'], cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    inventory = []
    try:
        for name in names:
            reader.stdin.write((commit + ':' + name + '\n').encode()); reader.stdin.flush()
            header = reader.stdout.readline().decode().split()
            assert len(header) == 3 and header[1] == 'blob', header
            blob = reader.stdout.read(int(header[2])); assert reader.stdout.read(1) == b'\n'
            source, seed = stage / 'source' / name, stage / 'seed' / name
            source.parent.mkdir(parents=True, exist_ok=True)
            if source.exists():
                assert source.read_bytes() == blob, 'Changed isolated source'
            else:
                source.write_bytes(blob)
            detail = prepare_file(source, seed)
            inventory.append({'path': name, 'source_sha256': checks.sha(source), 'working_sha256': checks.sha(ROOT / name), **detail})
            source.unlink()  # Rehydrate one exact Git blob per worker below.
    finally:
        reader.stdin.close(); reader.wait()
    (stage / ('inventory-factory.json' if args.factory_only else 'inventory.json')).write_text(json.dumps(inventory, indent=2), encoding='utf-8')
    print('Prepared', len(inventory), 'factory documents;', sum(x['sketches'] for x in inventory), 'Sketches;', sum(x['flipped'] for x in inventory), 'coordinate quantities', flush=True)
    if args.prepare_only:
        return
    runtime_hash = checks.sha(native)
    previous = {}
    report_path = stage / ('verification-factory.json' if args.factory_only else 'verification.json')
    if args.resume and report_path.exists():
        prior = json.loads(report_path.read_text(encoding='utf-8'))
        assert prior['native_sha256'] == runtime_hash and prior['commit'] == commit
        previous = {row['path']: row for row in prior['assets'] if row['ok']}
    reports = stage / 'reports'; reports.mkdir(exist_ok=True)
    environment = os.environ | {'QT_QPA_PLATFORM': 'offscreen', 'QT_STYLE_OVERRIDE': 'Fusion'}

    def verify(item):
        name = item['path']; start = time.monotonic()
        source, seed, output = (stage / folder / name for folder in ('source', 'seed', 'verified'))
        prior = previous.get(name)
        if prior and output.exists() and checks.sha(output) == prior['output_sha256']:
            return prior
        try:
            if not source.exists():
                source.parent.mkdir(parents=True,exist_ok=True)
                source.write_bytes(subprocess.check_output(['git','show',commit+':'+name],cwd=ROOT))
            assert checks.sha(source)==item['source_sha256'],'Changed isolated committed source'
            evidence_path = reports / (hashlib.sha256(name.encode()).hexdigest() + '.json')
            template_fixture = name.startswith('tests/fixtures/drawing-library/')
            mode = '--rewrite-template-fixture' if template_fixture else '--rewrite-sketch-fixture' if output.suffix == '.json' else '--rewrite-native-asset' if output.suffix in {'.asmz', '.drwz'} else '--reexpress-coordinate-asset'
            # This is an owned disposable output. Do not accumulate automatic
            # document backup rotations while repeating native verification.
            if output.exists():
                output.unlink()
            run = subprocess.run([str(native), mode, str(seed), str(output), str(evidence_path)], cwd=stage / 'seed', env=environment, capture_output=True, timeout=300)
            assert run.returncode == 0, (run.stdout + run.stderr).decode('utf-8', errors='replace')[-3000:]
            evidence = json.loads(evidence_path.read_text(encoding='utf-8'))
            assert evidence['native_readback_exact']
            if output.suffix not in {'.asmz', '.drwz', '.json'} and not template_fixture:
                before, after = checks.ini(source), checks.ini(output)
                old_ids,new_ids=checks.identifiers(before),checks.identifiers(after)
                assert all(new_ids.get(key)==number for key,number in old_ids.items()), 'Changed stable dimension allocations'
                added=new_ids.keys()-old_ids.keys()
                allowed={'parameter:direction'}|{'parameter:side'+str(side)+'_up_to_'+kind+'_correction' for side in (0,1) for kind in ('length','angle')}
                assert not added or name.startswith('cpp/tests/fixtures/') and all(key in allowed for owner,key in added), 'Unexpected dimension allocations'
                old_cache, new_cache = checks.cache(before), checks.cache(after)
                assert len(old_cache) == len(new_cache), 'Changed boundary count'
                for old, new in zip(old_cache, new_cache):
                    assert same_reference_geometry(old['original_references'],new['original_references']), 'Changed original geometry packet'
                    assert checks.identities(old) <= checks.identities(new), 'Lost source/side identity'
                    for key in ('volume', 'surface_area'):
                        a, b = old.get(key), new.get(key)
                        if isinstance(a, (int, float)) and isinstance(b, (int, float)):
                            assert math.isclose(a, b, rel_tol=1e-8, abs_tol=1e-6), (key, a, b)
                        else:
                            assert a == b, ('Changed deferred analysis', key)
                assert not evidence.get('errors'), 'Calculation errors'
            assert checks.sha(ROOT / name) == item['working_sha256'], 'Working/private asset changed during verification'
            return {'path': name, 'ok': True, 'seconds': time.monotonic() - start, 'output_sha256': checks.sha(output), 'evidence': evidence}
        except Exception as error:
            return {'path': name, 'ok': False, 'seconds': time.monotonic() - start, 'error': str(error)}
        finally:
            source.unlink(missing_ok=True)

    selected = inventory[:args.limit] if args.limit else inventory
    rows = []
    def publish():
        report_path.write_text(json.dumps({'commit': commit, 'native_sha256': runtime_hash, 'partial': len(selected) != len(inventory),
            'total': len(rows), 'passed': sum(row['ok'] for row in rows), 'assets': rows}, indent=2), encoding='utf-8')
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for row in pool.map(verify, selected):
            rows.append(row)
            if not row['ok']:
                print('FAILED', row['path'], row['error'], flush=True)
            if len(rows) % 25 == 0:
                publish(); print('Verified', len(rows), '/', len(selected), flush=True)
    publish()
    print('Passed', sum(row['ok'] for row in rows), '/', len(rows), flush=True)
    if not all(row['ok'] for row in rows):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
