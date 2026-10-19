"""Verify repeated blind-cap-nut master edits on disposable native copies."""
import argparse
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil
import subprocess
from fastener_verification_runtime import pin_checker

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('cap',ROOT/'tools/generate-cap-nut-library.py')
cap=importlib.util.module_from_spec(spec);spec.loader.exec_module(cap)


def main():
    global cap
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--resume',action='store_true',help='Retain completed receipts and retry incomplete variants in separate folders')
    parser.add_argument('--din',action='store_true',help='Verify separate DIN 1587 hemisphere/hexagon candidates')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if args.din:
        spec=importlib.util.spec_from_file_location('din_cap',ROOT/'tools/generate-din-cap-nut-library.py')
        cap=importlib.util.module_from_spec(spec);spec.loader.exec_module(cap)
    report=json.loads((out/'results.json').read_text(encoding='utf-8')) if args.resume else []
    checker=pin_checker(out)
    for v in json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']:
        source=args.models/v['filename'];digest=hashlib.sha256(source.read_bytes()).hexdigest()
        previous=next((r for r in report if r['filename']==source.name),None)
        if previous:
            assert previous['source_sha256']==digest and previous['successive_edits']==15
            continue
        folder=out/source.stem
        if args.resume and folder.exists():
            attempt=1
            while folder.with_name(folder.name+'-retry-'+str(attempt)).exists():attempt+=1
            folder=folder.with_name(folder.name+'-retry-'+str(attempt))
        folder.mkdir(parents=True,exist_ok=True);target=folder/source.name
        assert not target.exists();shutil.copyfile(source,target)
        ids=json.loads((args.models/'evidence'/source.stem/'identities.json').read_text(encoding='utf-8'))
        native=cap.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
        case=copy.deepcopy(v);edits=0
        try:
            native.run('open',path=str(target))
            def references():
                return {(r['owner'],r['key']) for owner in (ids['prism'],ids['thread'])
                    for r in native.run('reference.list',kind='face',owner=owner,limit=5000)['items']}
            baseline=references()
            def check():
                if args.din:
                    case['bore_depth_mm']=cap.resolved_bore_depth(case)
                    case['pole_wall_mm']=case['height_mm']-case['bore_depth_mm']
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                assert references()==baseline,'Cap nut source face identity changed'
                opening=native.run('opening.get',container=ids['thread'])
                assert abs(opening['thread_length_mm']-case['thread_length_mm'])<1e-8
                assert abs(opening['bore_length_mm']-case['bore_depth_mm'])<1e-8
                stock=next(p['values'][''] for p in native.run('document.parameters.get')['parameters'] if p['key']=='stock')
                assert stock==f'M{case["diameter_mm"]:g}x{case["pitch_mm"]:g}'
                native.run('save');native.run('close');native.run('open',path=str(target));assert references()==baseline
                case['independent_volume_mm3']=cap.independent_volume(case)
                (folder/'manifest.json').write_text(json.dumps(dict(variants=[case]),indent=2),encoding='utf-8')
                result=subprocess.run([str(checker),'--cap-nuts',str(folder)],capture_output=True,timeout=240)
                (folder/'last-check.stdout.log').write_bytes(result.stdout);(folder/'last-check.stderr.log').write_bytes(result.stderr)
                assert result.returncode==0,(source.name,result.stderr[-2000:])
                for backup in folder.glob(target.name+'.*'):
                    if backup.name[len(target.name)+1:].isdigit():
                        assert backup.resolve().parent==folder;backup.unlink()
            def get_height():return native.run('feature.get',container=ids['prism'])['parameters']['sides'][0]['length']
            def set_height(value):
                sides=native.run('feature.get',container=ids['prism'])['parameters']['sides'];sides[0]['length']=value
                native.run('feature.set',container=ids['prism'],parameters=dict(sides=sides))
            drivers=[('hex_height_mm' if args.din else 'height_mm',get_height,set_height,1,(1.02,1.04,1))]
            if args.din:
                sk,dim=ids['crown']['sketch'],ids['crown']['height']
                drivers.append(('height_mm',lambda sk=sk,dim=dim:native.run('sketch.dimension.get',sketch=sk,dimension=dim)['value'],
                    lambda value,sk=sk,dim=dim:native.run('sketch.dimension.set',sketch=sk,dimension=dim,value=value),1,(1.02,1.04,1)))
            for key,sk,dim,factor in [('across_flats_mm',ids['sketch'],ids['size'],math.sqrt(3)/2),
                ('dome_radius_mm',ids['crown']['sketch'],ids['crown']['radius'],1)]:
                drivers.append((key,lambda sk=sk,dim=dim:native.run('sketch.dimension.get',sketch=sk,dimension=dim)['value'],
                    lambda value,sk=sk,dim=dim:native.run('sketch.dimension.set',sketch=sk,dimension=dim,value=value),factor,(1.02,1.04,1)))
            opening_drivers=[('thread_length_mm','thread_length_mm',(.98,.96,1))] if args.din else [
                ('bore_depth_mm','bore_length_mm',(.98,.96,1)),('thread_length_mm','thread_length_mm',(1.02,1.04,1))]
            for key,parameter,scales in opening_drivers:
                drivers.append((key,lambda parameter=parameter:native.run('opening.get',container=ids['thread'])[parameter],
                    lambda value,parameter=parameter:native.run('opening.set',container=ids['thread'],**{parameter:value}),1,scales))
            check()
            for key,get,change,factor,scales in drivers:
                for scale in scales:
                    value=v[key]*scale/factor;before=get();change(value);assert abs(get()-value)<1e-8
                    native.run('undo');assert abs(get()-before)<1e-8
                    native.run('redo');assert abs(get()-value)<1e-8
                    case[key]=value*factor;check();edits+=1
                    print(f'Checked {source.name}: {key}={case[key]:g}',flush=True)
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
        report.append(dict(filename=source.name,source_sha256=digest,successive_edits=edits,
            source_face_identity=True,independent_volume_and_mass=True,blind_end_wall=True,
            sphere_equation=True,thread_extent=True,derived_stock=True,undo_redo=True,save_reopen=True,
            locked_unlocked_profile_dragging=True,gui_verified=False))
        (out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')


if __name__=='__main__':main()
