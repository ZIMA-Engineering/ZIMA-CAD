"""Exercise the authored ISO 4017 library through the actual native CLI.

Only independent copies in build/ are edited. Factory files remain read-only.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = ROOT / 'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS/01-HEX-HEAD/03-FULL-AB'
SHAFT = '01a14429e3207acba34fc9c038cdcc7f'
HEAD_SKETCH = '01a14429e3207acba34fc9c038cdccd3'
THREAD = '01a14429e3207acba34fc9c038cdccfe'
VARIANTS = [(3, 20, .5), (4, 20, .7), (5, 20, .8), (6, 25, 1.)]


def filename(diameter, length):
    diameter_text=f'{diameter:04.1f}' if diameter in (2,3) else f'{diameter:02d}'
    return f'HHS-ISO4017-M{diameter_text}x{length:03d}.prtz'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    args = parser.parse_args()
    work = ROOT / 'build' / ('fastener-library-check-' + uuid.uuid4().hex)
    work.mkdir(parents=True)
    environment = os.environ | {'TEMP': str(work), 'TMP': str(work)}
    results = []
    sources = {LIBRARY / filename(d,length): None for d, length, _ in VARIANTS}
    for path in sources:
        sources[path] = hashlib.sha256(path.read_bytes()).hexdigest()
        matches = list((ROOT / 'config/lib/03-ENGINEERING-TABLES').rglob('*.prtz'))
        assert sum(p.name.casefold() == path.name.casefold() for p in matches) == 1, path
    with (work / 'commands.jsonl').open('w', encoding='utf-8') as log, (work / 'stderr.log').open('w', encoding='utf-8') as errors:
        process = subprocess.Popen([str(args.cli.resolve()), '--stdin'], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=errors, text=True, encoding='utf-8', env=environment)
        def run(command, **arguments):
            process.stdin.write(json.dumps({'command': command, 'arguments': arguments}) + '\n')
            process.stdin.flush()
            line = process.stdout.readline()
            log.write(line); log.flush()
            response = json.loads(line)
            assert response['ok'], (command, response)
            return response['data']
        def supports():
            entities = run('sketch.entities', sketch=HEAD_SKETCH)['items']
            references = [run('sketch.entity.get', sketch=HEAD_SKETCH, entity=e['id'])['entity']
                          for e in entities if e['kind'] == 'external_reference']
            assert len(references) == 2 and all(not r['broken'] for r in references)
            return references
        def length_value():
            return run('feature.get', container=SHAFT)['parameters']['sides'][0]['length']
        try:
            for diameter, original, pitch in VARIANTS:
                source = LIBRARY / filename(diameter,original)
                copy = work / source.name
                run('open', path=str(source)); run('save_as', path=str(copy)); run('open', path=str(copy))
                references = supports()
                thread = run('shaft_thread.get', container=THREAD)
                assert thread['end_condition'] == 'up_to' and not thread['runout_enabled']
                assert thread['nominal_diameter_mm'] == diameter and abs(thread['pitch_mm'] - pitch) < 1e-9
                parameters = {p['key']: p for p in run('document.parameters.get')['parameters']}
                assert set(parameters['name']['values']) == {'cs', 'en', 'de', 'fr', 'ru'}
                for key, expected in [('standard', 'ISO 4017'), ('material', '8.8'), ('quantity', '-')]:
                    assert parameters[key]['values'][''] == expected
                for length in (original + 5, original + 10, original):
                    before = length_value()
                    sides = run('feature.get', container=SHAFT)['parameters']['sides']
                    sides[0]['length'] = length
                    run('feature.set', container=SHAFT, parameters={'sides': sides})
                    assert length_value() == length
                    run('undo'); assert length_value() == before
                    run('redo'); assert length_value() == length
                    run('regenerate')
                    assert supports() == references, 'Shaft edit changed read-only head supports'
                    changed = run('shaft_thread.get', container=THREAD)
                    assert changed['end_condition'] == 'up_to'
                    assert all(changed[key] == thread[key] for key in ('cylinder', 'start', 'end', 'chamfer'))
                    parameters = {p['key']: p for p in run('document.parameters.get')['parameters']}
                    assert parameters['stock']['values'][''] == f'M{diameter}x{length}'
                    run('save'); run('close'); run('open', path=str(copy))
                    assert length_value() == length and supports() == references
                run('close')
                results.append({'file': source.name, 'successive_lengths': [original + 5, original + 10, original],
                                'pitch_mm': pitch, 'undo_redo': True, 'save_reopen': True, 'references_unchanged': True})
        finally:
            process.stdin.close()
            process.wait(timeout=60)
            for source, before in sources.items():
                assert hashlib.sha256(source.read_bytes()).hexdigest() == before, 'Factory source changed'
    (work / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'variants': len(results), 'successive_edits': len(results) * 3, 'evidence': str(work)}))


if __name__ == '__main__':
    main()
