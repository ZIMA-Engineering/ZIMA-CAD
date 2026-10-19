"""Check repeated native nut width, height and catalog edits on disposable copies."""
import argparse
import copy
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('nuts',ROOT/'tools/generate-hex-nut-library.py')
nuts=importlib.util.module_from_spec(spec);spec.loader.exec_module(nuts)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--filename')
    p.add_argument('--standard')
    p.add_argument('--workers',type=int,choices=(1,2,3),default=1)
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    p.add_argument('--checker',type=Path,default=ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe');args=p.parse_args()
    source=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((source/'manifest.json').read_text(encoding='utf-8'))['variants'];report=[]
    if args.workers>1:
        assert not args.filename and not args.standard,'Use one worker for a filtered run'
        def group(standard):
            folder=out/standard.replace(' ','');folder.mkdir(parents=True,exist_ok=True)
            with (folder/'stdout.log').open('w',encoding='utf-8') as log:
                subprocess.run([sys.executable,str(Path(__file__).resolve()),'--stage',str(source),'--output',str(folder),
                    '--standard',standard,'--cli',str(args.cli.resolve()),'--checker',str(args.checker.resolve())],
                    stdout=log,stderr=subprocess.STDOUT,check=True)
            result=json.loads((folder/'results.json').read_text(encoding='utf-8'))
            print(f'Completed {standard}: {len(result)} nut variants',flush=True)
            return result
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            for result in pool.map(group,sorted({v['standard'] for v in rows})):report.extend(result)
        assert len(report)==len(rows) and len({r['filename'] for r in report})==len(rows)
        (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(f'{len(report)} hex nuts; {sum(r["successive_edits"]+r["catalog_edits"] for r in report)} independently checked edits',flush=True)
        return
    for v in rows:
        if args.filename and v['filename']!=args.filename:continue
        if args.standard and v['standard']!=args.standard:continue
        original=source/v['filename'];digest=hashlib.sha256(original.read_bytes()).hexdigest();target=out/original.name
        assert not target.exists();shutil.copyfile(original,target)
        ids=json.loads((source/'evidence'/original.stem/'identities.json').read_text(encoding='utf-8'))
        native=nuts.caps.studs.api.Native(args.cli.resolve(),out/'evidence'/original.stem)
        try:
            native.run('open',path=str(target))
            def references():return {(r['owner'],r['key']) for r in native.run('reference.list',kind='face')['items']}
            baseline=references();case=copy.deepcopy(v)
            def height():return native.run('feature.get',container=ids['prism'])['parameters']['sides'][0]['length']
            def size():return native.run('sketch.dimension.get',sketch=ids['sketch'],dimension=ids['size'])['value']
            def set_height(value):
                sides=native.run('feature.get',container=ids['prism'])['parameters']['sides'];sides[0]['length']=value
                native.run('feature.set',container=ids['prism'],parameters=dict(sides=sides))
            cases=[('height_mm',height,set_height,v['height_mm']*f) for f in (1.1,1.2,1)]
            cases.extend(('across_flats_mm',size,lambda value:native.run('sketch.dimension.set',sketch=ids['sketch'],dimension=ids['size'],value=value),
                2*v['across_flats_mm']*f/(3**.5)) for f in (1.03,1.06,1))
            for key,get,change,value in cases:
                before=get();change(value);assert abs(get()-value)<1e-8
                native.run('undo');assert abs(get()-before)<1e-8
                native.run('redo');assert abs(get()-value)<1e-8
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                assert references()==baseline,'Changed nut face identity'
                case[key]=value if key=='height_mm' else value*(3**.5)/2
                opening=native.run('opening.get',container=ids['thread'])
                assert abs(opening['thread_length_mm']-case['height_mm'])<1e-8,'Thread does not follow nut height'
                assert abs(opening['bore_length_mm']-case['height_mm'])<1e-8,'Bore does not follow nut height'
                params=native.run('document.parameters.get')['parameters'];stock=next(p['values'][''] for p in params if p['key']=='stock')
                assert stock==f'M{v["diameter_mm"]:g}x{v["pitch_mm"]:g}',stock
                native.run('save');native.run('close');native.run('open',path=str(target));assert references()==baseline
                (out/'manifest.json').write_text(json.dumps(dict(variants=[case])),encoding='utf-8')
                subprocess.run([str(args.checker.resolve()),'--hex-nuts',str(out)],check=True,stdout=subprocess.DEVNULL)
            catalog=native.run('thread.catalog',standard='metric',limit=1000)['items']
            base=next(t for t in catalog if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and abs(t['pitch_mm']-v['pitch_mm'])<1e-8)
            alternatives=[t for t in catalog if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and t['designation']!=base['designation']]
            edits=alternatives[:2]+[base] if alternatives else []
            other=[t for t in catalog if t['nominal_diameter_mm']!=v['diameter_mm'] and t['internal_root_diameter_mm']+2*v['mouth_chamfer_mm']<v['across_flats_mm']]
            if other:edits.extend([min(other,key=lambda t:abs(t['nominal_diameter_mm']-v['diameter_mm'])),base])
            for selected in edits:
                before=native.run('opening.get',container=ids['thread'])['designation']
                native.run('opening.set',container=ids['thread'],designation=selected['designation'])
                native.run('undo');assert native.run('opening.get',container=ids['thread'])['designation']==before
                native.run('redo');assert native.run('opening.get',container=ids['thread'])['designation']==selected['designation']
                native.run('regenerate');assert not native.run('history.list')['calculation_errors'] and references()==baseline
                case.update(diameter_mm=selected['nominal_diameter_mm'],pitch_mm=selected['pitch_mm'],bore_mm=selected['internal_root_diameter_mm'])
                params=native.run('document.parameters.get')['parameters'];stock=next(p['values'][''] for p in params if p['key']=='stock')
                assert stock==f'M{case["diameter_mm"]:g}x{case["pitch_mm"]:g}',stock
                native.run('save');native.run('close');native.run('open',path=str(target));assert references()==baseline
                (out/'manifest.json').write_text(json.dumps(dict(variants=[case])),encoding='utf-8')
                subprocess.run([str(args.checker.resolve()),'--hex-nuts',str(out)],check=True,stdout=subprocess.DEVNULL)
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(original.read_bytes()).hexdigest()==digest
        report.append(dict(filename=v['filename'],source_sha256=digest,successive_edits=6,undo_redo=True,save_reopen=True,
            reference_identity=True,analytical_volume_after_each_edit=True,thread_follows_height=True,derived_stock=True,catalog_edits=len(edits)))
        (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print('Verified repeated nut height and width edits '+v['filename'],flush=True)
    print(f'{len(report)} hex nuts; {6*len(report)+sum(r["catalog_edits"] for r in report)} independently checked edits',flush=True)


if __name__=='__main__':main()
