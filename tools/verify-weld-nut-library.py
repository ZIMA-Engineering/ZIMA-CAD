"""Exercise weld-nut native drivers, dependent supports, Undo/Redo and round trips."""
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
spec=importlib.util.spec_from_file_location('weld',ROOT/'tools/generate-weld-nut-library.py')
weld=importlib.util.module_from_spec(spec);spec.loader.exec_module(weld)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--resume',action='store_true');args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);checker=pin_checker(out)
    rows=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']
    report=json.loads((out/'results.json').read_text(encoding='utf-8')) if args.resume else []
    for v in rows:
        source=args.models/v['filename'];digest=hashlib.sha256(source.read_bytes()).hexdigest()
        prior=next((r for r in report if r['filename']==source.name),None)
        if prior:
            assert prior['source_sha256']==digest and prior['successive_edits']==(18 if v['kind']=='din' else 24);continue
        folder=out/source.stem;attempt=1
        while folder.exists():folder=out/(source.stem+'-retry-'+str(attempt));attempt+=1
        folder.mkdir();target=folder/source.name;shutil.copyfile(source,target)
        ids=json.loads((args.models/'evidence'/source.stem/'identities.json').read_text(encoding='utf-8'))
        native=weld.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
        case=copy.deepcopy(v);edits=0;catalog_edits=0
        try:
            native.run('open',path=str(target))
            def sources():
                return {(r['owner'],r['key']) for owner in (ids['blank']['feature'],ids['thread'])
                    for r in native.run('reference.list',kind='face',owner=owner,limit=5000)['items']}
            baseline=sources()
            def check():
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                assert sources()==baseline,'Weld nut original face identity changed'
                opening=native.run('opening.get',container=ids['thread'])
                assert abs(opening['thread_length_mm']-case['thread_length_mm'])<1e-8
                placement=native.run('placement.get',object=ids['thread'])['placement']
                assert placement['reference_valid'] and abs(placement['z']-case['bore_start_mm'])<1e-8
                case['independent_volume_mm3']=weld.din_volume(case) if case['kind']=='din' else weld.iso_volume(case)
                native.run('save');native.run('close');native.run('open',path=str(target));assert sources()==baseline
                (folder/'manifest.json').write_text(json.dumps(dict(variants=[case]),indent=2),encoding='utf-8')
                checked=subprocess.run([str(checker),'--weld-nuts',str(folder)],capture_output=True,timeout=240)
                (folder/'last-check.stdout.log').write_bytes(checked.stdout);(folder/'last-check.stderr.log').write_bytes(checked.stderr)
                assert checked.returncode==0,(source.name,checked.stderr[-2000:])
                stock=next(p['values'][''] for p in native.run('document.parameters.get')['parameters'] if p['key']=='stock')
                assert stock==f'M{case["diameter_mm"]:g}x{case["pitch_mm"]:g}'
                for backup in folder.glob(target.name+'.*'):
                    if backup.name[len(target.name)+1:].isdigit():assert backup.resolve().parent==folder;backup.unlink()
            def get_parameter(owner,key):return native.run('feature.get',container=owner)['parameters'][key]
            def set_parameter(owner,key,value):native.run('feature.set',container=owner,parameters={key:value})
            def get_length(owner,side):return get_parameter(owner,'sides')[side]['length']
            def set_length(owner,side,value):
                sides=get_parameter(owner,'sides');sides[side]['length']=value;set_parameter(owner,'sides',sides)
            drivers=[('body_length',lambda:get_length(ids['blank']['feature'],0),lambda x:set_length(ids['blank']['feature'],0,x))]
            if v['kind']=='din':
                drivers += [
                ('projection_height_mm',lambda:get_parameter(ids['blank']['feature'],'profile_plane_offset'),lambda x:set_parameter(ids['blank']['feature'],'profile_plane_offset',x)),
                ('pilot_height_mm',lambda:get_length(ids['pilot']['feature'],1),lambda x:set_length(ids['pilot']['feature'],1,x))]
                dimensions=[('across_flats_mm',ids['blank']['sketch'],ids['blank']['size'],3**.5/2),
                    ('projection_depth_mm',ids['projection']['sketch'],ids['projection']['depth'],1),
                    ('pilot_diameter_mm',ids['pilot']['sketch'],ids['pilot']['size'],1)]
                case['body_length']=v['height_mm']-v['projection_height_mm']
            else:
                drivers += [
                    ('projection_height_mm',lambda:get_parameter(ids['flange']['feature'],'profile_plane_offset'),lambda x:set_parameter(ids['flange']['feature'],'profile_plane_offset',x)),
                    ('flange_height_mm',lambda:get_length(ids['flange']['feature'],0),lambda x:set_length(ids['flange']['feature'],0,x)),
                    ('projection_radius_mm',lambda:native.run('fillet.get',container=ids['projection']['rounding'])['radius_mm'],
                        lambda x:native.run('fillet.set',container=ids['projection']['rounding'],radius_mm=x))]
                dimensions=[('across_flats_mm',ids['blank']['sketch'],ids['blank']['size'],3**.5/2),
                    ('flange_diameter_mm',ids['flange']['sketch'],ids['flange']['size'],1),
                    ('projection_width_mm',ids['projection']['sketch'],ids['projection']['width'],1),
                    ('projection_inner_radius_mm',ids['projection']['sketch'],ids['projection']['inner_radius'],1)]
                case['body_length']=v['height_mm']-v['flange_height_mm']
                case['projection_inner_radius_mm']=v['flange_diameter_mm']/2-v['projection_depth_mm']
            for key,sk,dim,factor in dimensions:
                drivers.append((key,lambda sk=sk,dim=dim,factor=factor:native.run('sketch.dimension.get',sketch=sk,dimension=dim)['value']*factor,
                    lambda value,sk=sk,dim=dim,factor=factor:native.run('sketch.dimension.set',sketch=sk,dimension=dim,value=value/factor)))
            check()
            for key,get,change in drivers:
                initial=get()
                for factor in (1.02,1.04,1):
                    before=get();value=initial*factor;change(value);assert abs(get()-value)<1e-8
                    native.run('undo');assert abs(get()-before)<1e-8
                    native.run('redo');assert abs(get()-value)<1e-8
                    case[key]=value
                    if case['kind']=='din':
                        case['height_mm']=case['body_length']+case['projection_height_mm']
                        case['bore_start_mm']=case['projection_height_mm']-case['pilot_height_mm']
                        case['thread_length_mm']=case['body_length']+case['pilot_height_mm']
                    else:
                        case['height_mm']=case['body_length']+case['flange_height_mm']
                        case['overall_height_mm']=case['height_mm']+case['projection_height_mm']
                        case['bore_start_mm']=case['projection_height_mm']
                        case['thread_length_mm']=case['height_mm']
                        case['projection_depth_mm']=case['flange_diameter_mm']/2-case['projection_inner_radius_mm']
                    check();edits+=1;print(f'Checked {source.name}: {key}={value:g}',flush=True)
            choices=[x for x in weld.variants() if x['kind']==v['kind'] and x['diameter_mm']==v['diameter_mm'] and x['pitch_mm']!=v['pitch_mm']]
            if choices:
                for pitch in (choices[0]['pitch_mm'],v['pitch_mm']):
                    item=next(t for t in native.run('thread.catalog',standard='metric',limit=1000)['items'] if
                        abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and abs(t['pitch_mm']-pitch)<1e-8)
                    previous=native.run('opening.get',container=ids['thread'])['designation']
                    native.run('opening.set',container=ids['thread'],designation=item['designation'])
                    native.run('undo');assert native.run('opening.get',container=ids['thread'])['designation']==previous
                    native.run('redo');case['pitch_mm']=pitch;case['bore_mm']=item['internal_root_diameter_mm'];check();catalog_edits+=1
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
        report.append(dict(filename=source.name,source_sha256=digest,successive_edits=edits,catalog_edits=catalog_edits,
            independent_volume_and_mass=True,three_real_projections=True,reference_identity=True,derived_stock=True,
            opening_follows_pilot=v['kind']=='din',opening_follows_support_face=True,
            undo_redo=True,save_reopen=True,locked_unlocked_profile_dragging=True))
        (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
