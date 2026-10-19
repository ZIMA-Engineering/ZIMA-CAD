"""Check repeated native castle-nut edits, independent sections and thread trim."""
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
spec=importlib.util.spec_from_file_location('castle',ROOT/'tools/generate-castle-nut-library.py')
castle=importlib.util.module_from_spec(spec);spec.loader.exec_module(castle)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--filename')
    p.add_argument('--workers',type=int,choices=(1,2,3),default=1)
    p.add_argument('--partition',type=int);args=p.parse_args()
    stage=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);report=[]
    checker=ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe'
    rows=json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants']
    if args.workers>1 and args.partition is None:
        assert not args.filename
        def group(index):
            folder=out/str(index);folder.mkdir(parents=True,exist_ok=True)
            with (folder/'stdout.log').open('w',encoding='utf-8') as log:
                subprocess.run([sys.executable,str(Path(__file__).resolve()),'--stage',str(stage),'--output',str(folder),
                    '--workers',str(args.workers),'--partition',str(index)],stdout=log,stderr=subprocess.STDOUT,check=True)
            return json.loads((folder/'results.json').read_text(encoding='utf-8'))
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            for result in pool.map(group,range(args.workers)):report.extend(result)
        assert len(report)==len(rows) and len({r['filename'] for r in report})==len(rows)
        (out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(f'{len(report)} castle nuts: {sum(r["successive_edits"]+r["catalog_edits"] for r in report)} checked edits',flush=True)
        return
    for index,v in enumerate(rows):
        if args.partition is not None and index%args.workers!=args.partition:continue
        if args.filename and args.filename!=v['filename']:continue
        source=stage/v['filename'];digest=hashlib.sha256(source.read_bytes()).hexdigest()
        folder=out/source.stem;folder.mkdir(parents=True,exist_ok=True);target=folder/source.name
        assert not target.exists();shutil.copyfile(source,target)
        ids=json.loads((stage/'evidence'/source.stem/'identities.json').read_text(encoding='utf-8'))
        native=castle.nuts.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
        case=copy.deepcopy(v);count=0
        try:
            native.run('open',path=str(target))
            def references():return {(r['owner'],r['key']) for r in native.run('reference.list',kind='face')['items']}
            baseline=references()
            def dimension(sk,dim):return native.run('sketch.dimension.get',sketch=sk,dimension=dim)['value']
            def set_dimension(sk,dim,value):native.run('sketch.dimension.set',sketch=sk,dimension=dim,value=value)
            def height():return native.run('feature.get',container=ids['prism'])['parameters']['sides'][0]['length']
            def set_height(value):
                sides=native.run('feature.get',container=ids['prism'])['parameters']['sides'];sides[0]['length']=value
                native.run('feature.set',container=ids['prism'],parameters=dict(sides=sides))
            first=ids['slots'][0]
            drivers=[('height_mm',height,set_height,1)]
            for key,sk,dim,factor in [
                ('across_flats_mm',ids['sketch'],ids['size'],3**.5/2),
                ('slot_width_mm',first['sketch'],first['width'],1),
                ('slot_floor_mm',first['sketch'],first['y'],-1)]:
                drivers.append((key,lambda sk=sk,dim=dim:dimension(sk,dim),
                    lambda value,sk=sk,dim=dim:set_dimension(sk,dim,value),factor))
            if 'crown' in ids and not v.get('crown_diameter_from_flats'):
                sk,dim=ids['crown']['sketch'],ids['crown']['radius']
                drivers.append(('crown_diameter_mm',lambda:dimension(sk,dim),lambda value:set_dimension(sk,dim,value),2))
            def verify():
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                assert references()==baseline,'Castle nut face reference identity changed'
                for slot in ids['slots']:
                    assert abs(dimension(slot['sketch'],slot['width'])-case['slot_width_mm'])<1e-8
                    assert abs(dimension(slot['sketch'],slot['y'])+case['slot_floor_mm'])<1e-8
                case['independent_volume_mm3']=castle.sections.volume(case)
                opening=native.run('opening.get',container=ids['thread'])
                assert abs(opening['thread_length_mm']-case['height_mm'])<1e-8
                assert abs(opening['bore_length_mm']-case['height_mm'])<1e-8
                parameters=native.run('document.parameters.get')['parameters']
                stock=next(p['values'][''] for p in parameters if p['key']=='stock')
                assert stock==f'M{case["diameter_mm"]:g}x{case["pitch_mm"]:g}',stock
                native.run('save');native.run('close');native.run('open',path=str(target))
                assert references()==baseline,'Reopened castle nut face identity changed'
                (folder/'manifest.json').write_text(json.dumps(dict(variants=[case])),encoding='utf-8')
                subprocess.run([str(checker),'--castle-nuts',str(folder)],check=True,stdout=subprocess.DEVNULL)
            for key,get,change,factor in drivers:
                initial=get()
                for multiple in (1.02,1.04,1):
                    before=get();value=initial*multiple;change(value);assert abs(get()-value)<1e-8
                    native.run('undo');assert abs(get()-before)<1e-8
                    native.run('redo');assert abs(get()-value)<1e-8
                    case[key]=value*factor
                    if case.get('crown_diameter_from_flats'):case['crown_diameter_mm']=case['across_flats_mm']
                    verify();count+=1
            catalog=native.run('thread.catalog',standard='metric',limit=1000)['items']
            base=next(t for t in catalog if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and abs(t['pitch_mm']-v['pitch_mm'])<1e-8)
            alternatives=[t for t in catalog if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and
                t['designation']!=base['designation'] and t['internal_root_diameter_mm']/2>v['slot_width_mm']]
            edits=alternatives[:2]+[base] if alternatives else []
            for selected in edits:
                before=native.run('opening.get',container=ids['thread'])['designation']
                native.run('opening.set',container=ids['thread'],designation=selected['designation'])
                native.run('undo');assert native.run('opening.get',container=ids['thread'])['designation']==before
                native.run('redo');assert native.run('opening.get',container=ids['thread'])['designation']==selected['designation']
                case.update(diameter_mm=selected['nominal_diameter_mm'],pitch_mm=selected['pitch_mm'],bore_mm=selected['internal_root_diameter_mm'])
                verify()
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
        report.append(dict(filename=v['filename'],source_sha256=digest,successive_edits=count,
            catalog_edits=len(edits),reference_identity=True,derived_stock=True,thread_follows_height=True,
            undo_redo=True,save_reopen=True,independent_volume_after_each_edit=True,trimmed_thread_after_each_edit=True))
        (out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(f'Checked {source.name}: {count} edits and thread trims',flush=True)


if __name__=='__main__':main()
