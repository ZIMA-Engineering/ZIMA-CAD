"""Check repeated length editing of independent native fastener candidates."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fastener_author', ROOT/'tools/generate-fastener-library.py')
author = importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--checker', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--stage', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--filename')
    parser.add_argument('--resume',action='store_true',help='Resume verified cases from this tool\'s evidence folder')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    results_path=output/'results.json'
    completed = json.loads(results_path.read_text(encoding='utf-8')) if args.resume and results_path.exists() else []
    completed_by_name={v['filename']:v for v in completed}
    for variant in manifest['variants']:
        if args.filename and variant['filename'] != args.filename:
            continue
        sources = [p.resolve()/variant['filename'] for p in args.stage if (p/variant['filename']).is_file()]
        if len(sources) != 1:
            raise RuntimeError(f'Expected exactly one candidate: {variant["filename"]}')
        source = sources[0]
        original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        if source.name in completed_by_name:
            if completed_by_name[source.name]['source_sha256']!=original_hash:
                raise RuntimeError('Previously verified source changed')
            continue
        target = output/source.name
        if target.exists():
            if not args.resume or not (output/'evidence'/source.stem/'commands.jsonl').is_file():
                raise RuntimeError(f'Use a fresh verification folder: {target}')
            interrupted=target.with_suffix('.prtz.interrupted')
            if interrupted.exists():raise RuntimeError('Interrupted copy already retained')
            target.rename(interrupted)
        shutil.copyfile(source, target)
        native = author.Native(args.cli.resolve(), output/'evidence'/source.stem)
        try:
            native.run('open', path=str(target))
            before_thread = native.run('shaft_thread.get', container=author.THREAD)
            def length():
                return native.run('feature.get', container=author.SHAFT)['parameters']['sides'][0]['length']
            original = variant['length_mm']
            step = max(5, 2*variant['pitch_mm'])
            for value in (original+step, original+2*step, original):
                previous = length()
                sides = native.run('feature.get', container=author.SHAFT)['parameters']['sides']
                sides[0]['length'] = value
                native.run('feature.set', container=author.SHAFT, parameters={'sides':sides})
                assert abs(length()-value)<1e-8
                native.run('undo'); assert abs(length()-previous)<1e-8
                native.run('redo'); assert abs(length()-value)<1e-8
                native.run('regenerate')
                assert not native.run('history.list')['calculation_errors']
                thread = native.run('shaft_thread.get', container=author.THREAD)
                for field in ('cylinder','start','end','chamfer','designation','pitch_mm','end_condition'):
                    assert thread[field] == before_thread[field], (source.name, field)
                params = {p['key']:p for p in native.run('document.parameters.get')['parameters']}
                stock = f'M{variant["diameter_mm"]:g}'
                if variant['fine_pitch']:
                    stock += f'x{variant["pitch_mm"]:g}'
                assert params['stock']['values'][''] == stock+f'x{value:g}'
                assert float(params['mass']['values']['']) > 0
                native.run('save'); native.run('close')
                changed = copy.deepcopy(variant)
                changed['length_mm'] = value
                probe_manifest = output/'current-manifest.json'
                probe_manifest.write_text(json.dumps({'variants':[changed]},ensure_ascii=False),encoding='utf-8')
                check = subprocess.run([str(args.checker.resolve()),str(probe_manifest),str(output)],
                    capture_output=True,text=True,encoding='utf-8')
                if check.returncode:
                    raise RuntimeError(check.stdout+check.stderr)
                native.run('open',path=str(target))
                assert abs(length()-value)<1e-8
            native.run('close')
        finally:
            native.close()
            if hashlib.sha256(source.read_bytes()).hexdigest()!=original_hash:
                raise RuntimeError('Verification changed its source candidate')
        completed.append(dict(filename=source.name,source_sha256=original_hash,
            lengths=[original+step,original+2*step,original],undo_redo=True,save_reopen=True,
            exact_geometry_after_each_edit=True,thread_references_retained=True))
        (output/'results.json').write_text(json.dumps(completed,indent=2),encoding='utf-8')
        # Only backups of this disposable verification copy are removed.
        for backup in output.glob(source.name+'.[123]'):
            backup.unlink()
        print(f'Edited, undo/redone and reopened {source.name}',flush=True)
    if not completed:
        parser.error('No variants selected')
    print(f'{len(completed)} fasteners, {3*len(completed)} independently checked successive length edits')


if __name__ == '__main__':
    main()
