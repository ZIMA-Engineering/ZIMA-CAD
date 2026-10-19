"""Record a finite edit matrix for unfinished wing studies, without promotion.

Each driver runs on a private copy. A failure is retained in the report and
cannot be mistaken for a completed factory-library gate.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
from fastener_verification_runtime import pin_checker

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('wing',ROOT/'tools/generate-wing-nut-library.py')
wing=importlib.util.module_from_spec(spec);spec.loader.exec_module(wing)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--filename')
    args=parser.parse_args();models=args.models.resolve();out=args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists()
    out.mkdir(parents=True);checker=pin_checker(out);report=[]
    rows=json.loads((models/'manifest.json').read_text(encoding='utf-8'))['variants']
    for v in rows:
        if args.filename and v['filename']!=args.filename:continue
        source=models/v['filename'];digest=hashlib.sha256(source.read_bytes()).hexdigest()
        ids=json.loads((models/'evidence'/source.stem/'identities.json').read_text(encoding='utf-8'))
        drivers=[('span_mm','wing','half_span',2),('height_mm','wing','height',-1),
            ('crown_radius_mm','wing','radius',1),('secondary_edge_radius_mm','wing','root_radius',1),
            ('hub_base_diameter_mm','hub','base_radius',2),('hub_top_diameter_mm','hub','top_radius',2),
            ('hub_height_mm','hub','height',-1),('root_thickness_mm','taper','root',2),
            ('tip_thickness_mm','taper','tip',2),('root_radius_mm',None,None,1)]
        for key,group,dimension,factor in drivers:
            folder=out/source.stem/key;folder.mkdir(parents=True);target=folder/source.name;shutil.copyfile(source,target)
            native=wing.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
            result=dict(filename=source.name,driver=key,source_sha256=digest,completed_edits=0,factory_ready=False)
            case=copy.deepcopy(v)
            try:
                native.run('open',path=str(target))
                def get():
                    if group:return native.run('sketch.dimension.get',sketch=ids[group]['sketch'],dimension=ids[group][dimension])['value']*factor
                    return native.run('fillet.get',container=ids['rounding']['root'])['radius_mm']
                def change(value):
                    if group:native.run('sketch.dimension.set',sketch=ids[group]['sketch'],dimension=ids[group][dimension],value=value/factor)
                    else:native.run('fillet.set',container=ids['rounding']['root'],radius_mm=value)
                def sources():
                    return {(r['owner'],r['key']) for owner in (ids['wing']['feature'],ids['hub']['feature'],ids['thread'])
                        for r in native.run('reference.list',kind='face',owner=owner,limit=5000)['items']}
                baseline=sources()
                initial=get();assert abs(initial-v[key])<1e-8,(key,initial,v[key])
                for scale in (1.02,1.04,1):
                    before=get();value=initial*scale;change(value);assert abs(get()-value)<1e-8
                    native.run('undo');assert abs(get()-before)<1e-8
                    native.run('redo');assert abs(get()-value)<1e-8
                    case[key]=value
                    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                    assert sources()==baseline,'Original source face identities changed'
                    for part in ('wing','taper','hub'):
                        status=native.run('sketch.solve_status',sketch=ids[part]['sketch'])
                        assert status['remaining_degrees_of_freedom']==0,status
                    opening=native.run('opening.get',container=ids['thread'])
                    assert abs(opening['thread_length_mm']-case['hub_height_mm'])<1e-8
                    native.run('save');native.run('close');native.run('open',path=str(target))
                    assert sources()==baseline and abs(get()-value)<1e-8
                    (folder/'manifest.json').write_text(json.dumps(dict(variants=[case]),indent=2),encoding='utf-8')
                    checked=subprocess.run([str(checker),'--wing-studies',str(folder)],capture_output=True,timeout=240)
                    (folder/'last-check.stdout.log').write_bytes(checked.stdout);(folder/'last-check.stderr.log').write_bytes(checked.stderr)
                    assert checked.returncode==0,checked.stderr.decode('utf-8',errors='replace')[-2000:]
                    result['completed_edits']+=1
                    print(f'Checked {source.name}: {key}={value:g}',flush=True)
                    for backup in folder.glob(source.name+'.*'):
                        if backup.name[len(source.name)+1:].isdigit():backup.unlink()
                result['passed']=True
                native.run('close')
            except (RuntimeError,AssertionError,subprocess.TimeoutExpired) as error:
                result.update(passed=False,error=str(error));print(f'Unfinished {source.name}: {key}: {error}',flush=True)
            finally:
                try:native.close()
                except RuntimeError as error:
                    result['native_exit']=str(error)
                    if result.get('passed'):raise
            assert hashlib.sha256(source.read_bytes()).hexdigest()==digest,'Verification changed its study source'
            report.append(result);(out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert report,'No matching studies'
    if not all(row['passed'] for row in report):raise SystemExit(1)


if __name__=='__main__':main()
