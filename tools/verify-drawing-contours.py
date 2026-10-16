"""Verify associative bolt contours through the native CLI on independent copies.

Factory/user input files remain read-only. Evidence is retained under build/.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
DRAWING = ROOT / 'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS/01-HEX-HEAD/03-FULL-AB/01-MODELS/0000-index/HHS-ISO4017-REFERENCE.drwz'
SHAFT = '01a14429e3207acba34fc9c038cdcc7f'
FILLET = 'c9305d0a-ba0e-4bea-b6a0-9a66ac6ebd2e'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--drawing', type=Path, default=DRAWING)
    args = parser.parse_args()
    drawing = args.drawing.resolve()
    work = ROOT / 'build' / ('drawing-contour-check-' + uuid.uuid4().hex)
    work.mkdir(parents=True)
    hashes = {drawing: hashlib.sha256(drawing.read_bytes()).hexdigest()}
    states = 0
    with (work / 'commands.jsonl').open('w', encoding='utf-8') as log, (work / 'stderr.log').open('w', encoding='utf-8') as errors:
        process = subprocess.Popen([str(args.cli.resolve()), '--stdin'], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=errors, text=True, encoding='utf-8',
                                   env=os.environ | {'TEMP': str(work), 'TMP': str(work)})

        def run(command, **arguments):
            process.stdin.write(json.dumps({'command': command, 'arguments': arguments}) + '\n')
            process.stdin.flush()
            line = process.stdout.readline()
            log.write(line); log.flush()
            response = json.loads(line)
            assert response['ok'], (command, response)
            return response['data']

        try:
            run('open', path=str(drawing))
            source_path = Path(run('drawing.view.list')['items'][0]['source_path'])
            if not source_path.is_absolute():
                source_path = drawing.parent / source_path
            hashes[source_path] = hashlib.sha256(source_path.read_bytes()).hexdigest()
            paths = run('save_as', path=str(work / 'contour.drwz'))['paths']
            part_copy = next(Path(p) for p in paths if p.lower().endswith('.prtz'))
            drawing_copy = next(Path(p) for p in paths if p.lower().endswith('.drwz'))
            part_id = next(d['id'] for d in run('open', path=str(part_copy)) if d['active'])
            radius = run('fillet.get', container=FILLET)['radius_mm']
            length = run('feature.get', container=SHAFT)['parameters']['sides'][0]['length']
            drawing_id = next(d['id'] for d in run('open', path=str(drawing_copy)) if d['active'])
            run('regenerate')
            view = run('drawing.view.list')['items'][0]
            curves = run('drawing.view.references', view=view['view'], kind='curve')['items']
            profiles = [c for c in curves if c['key'].startswith('drawing-contour:') and c.get('circle')]
            assert len(profiles) == 2
            for item in run('drawing.dimension.list')['items']:
                if item['kind'] == 'radius':
                    original = run('drawing.dimension.get', dimension=item['dimension'])
                    assert original['attachments'][0]['reference']['key'].startswith('drawing-contour:'), 'Existing radius must use its visible contour'
            dimensions = []
            for curve in profiles:
                created = run('drawing.dimension.create', view=view['view'], kind='radius', attachments=[{
                    'kind': 'curve_point', 'reference': {k: curve[k] for k in ('owner', 'key', 'instance_path')}, 'parameter': .5}])
                dimensions.append((created['dimension'], created['attachments']))

            def check(expected_radius):
                run('activate', document=drawing_id); run('regenerate')
                items = run('drawing.dimension.list')['items']
                assert all(item['state'] == 'resolved' for item in items), items
                assert all(abs(item['measurements'][0]['value'] - expected_radius) < 1e-8
                           for item in items if item['kind'] == 'radius')
                current = run('drawing.view.references', view=view['view'], kind='curve')['items']
                for dimension, attachments in dimensions:
                    value = run('drawing.dimension.get', dimension=dimension)
                    assert value['attachments'] == attachments
                    curve = next(c for c in current if all(c[k] == attachments[0]['reference'][k]
                                                          for k in ('owner', 'key', 'instance_path')))
                    circle = curve['circle']
                    project = lambda p, axis: sum(a * b for a, b in zip(p, view['camera'][axis]))
                    center = [project(circle['center'], axis) for axis in ('horizontal', 'vertical')]
                    rim = value['segments'][0]['last_presentation']['witness_second']
                    assert abs(math.hypot(rim[0] - center[0], rim[1] - center[1]) - expected_radius) < 1e-8
                    for i, axis in enumerate(('horizontal', 'vertical')):
                        ends = [project(curve[p], axis) for p in ('first', 'last')]
                        assert min(ends) - 1e-8 <= rim[i] <= max(ends) + 1e-8
                return items

            previous = radius
            for target in (radius * 1.2, radius * 1.5, radius):
                run('activate', document=part_id); run('fillet.set', container=FILLET, radius_mm=target)
                check(target)
                run('activate', document=part_id); run('undo'); check(previous)
                run('activate', document=part_id); run('redo'); check(target)
                previous = target; states += 3
            for target in (length + 5, length + 10, length):
                run('activate', document=part_id)
                sides = run('feature.get', container=SHAFT)['parameters']['sides']; sides[0]['length'] = target
                run('feature.set', container=SHAFT, parameters={'sides': sides})
                items = check(radius)
                assert any(i['measurements'][0]['text'] == 'l' and abs(i['measurements'][0]['value'] - target) < 1e-8 for i in items)
                states += 1
            for document in (part_id, drawing_id):
                run('activate', document=document); run('save'); run('close', document=document)
            run('open', path=str(drawing_copy)); check(radius)
        finally:
            process.stdin.close(); process.wait(timeout=60)
            for source, expected in hashes.items():
                assert hashlib.sha256(source.read_bytes()).hexdigest() == expected, 'Input document changed'
    result = {'source_states': states, 'save_reopen': True, 'inputs_unchanged': True, 'evidence': str(work)}
    (work / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
