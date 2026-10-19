"""Exercise repeated master edits on disposable shaped-fastener native Parts."""
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
spec=importlib.util.spec_from_file_location('flange',ROOT/'tools/generate-flange-nut-library.py')
flange=importlib.util.module_from_spec(spec);spec.loader.exec_module(flange)
wheel=flange.wheel


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('stage','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--filename');args=p.parse_args();stage=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants'];results=[]
    checker=pin_checker(out)
    for v in rows:
        if args.filename and v['filename']!=args.filename:continue
        source=stage/v['filename'];sha=hashlib.sha256(source.read_bytes()).hexdigest();case=copy.deepcopy(v)
        folder=out/source.stem;folder.mkdir();target=folder/source.name;shutil.copyfile(source,target)
        ids=json.loads((stage/'evidence'/source.stem/'identities.json').read_text(encoding='utf-8'))
        native=wheel.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
        try:
            native.run('open',path=str(target))
            references=lambda:{(r['owner'],r['key']) for r in native.run('reference.list',kind='face')['items']}
            baseline=references();plans=[]
            def dimension(profile,key,field,scale=1,factors=(1.01,1.02,1)):
                owner=ids[profile];sk=owner['sketch'];dim=owner[key]
                value=native.run('sketch.dimension.get',sketch=sk,dimension=dim)['value']
                get=lambda:native.run('sketch.dimension.get',sketch=sk,dimension=dim)['value']
                set_value=lambda value:native.run('sketch.dimension.set',sketch=sk,dimension=dim,value=value)
                plans.extend((field,get,set_value,value*f,lambda x:x*scale) for f in factors)
            if 'roof_angle_degrees' in v:
                dimension('flange','outside','flange_diameter_mm',2,factors=(1.005,1.01,1))
                dimension('flange','edge_height','flange_edge_height_mm',-1)
                dimension('flange','bearing_round','bearing_radius_mm',factors=(.9,.95,1))
                dimension('flange','lip_round','lip_radius_mm',factors=(.9,.95,1))
                mode='--flange-nuts';volume=flange.independent_volume
            elif v.get('state')=='compressed':
                dimension('profile','bore','bore_mm',2)
                dimension('profile','outside','outside_mm',2,factors=(1.001,1.002,1))
                dimension('profile','height','height_mm',-1)
                dimension('profile','radius','sphere_radius_mm')
                dimension('profile','transition','transition_diameter_mm',2)
                mode='--wheel-fasteners';volume=wheel.independent_washer_volume
            else:
                if v['kind']=='spherical-nuts':
                    dimension('collar','radius','sphere_radius_mm')
                    dimension('collar','end','end_diameter_mm',2)
                    dimension('collar','height','collar_height_mm',-1)
                else:
                    dimension('seat','end','end_diameter_mm',2)
                    dimension('seat','angle','included_angle_degrees',factors=(1.005,1.01,1))
                    plans[-3:]=[(key,get,change,value,lambda x:2*x-180) for key,get,change,value,convert in plans[-3:]]
                mode='--wheel-fasteners';volume=wheel.independent_nut_volume
            for field,get,change,value,convert in plans:
                before=get();change(value);assert abs(get()-value)<1e-8
                native.run('undo');assert abs(get()-before)<1e-8
                native.run('redo');assert abs(get()-value)<1e-8
                case[field]=convert(value)
                if v.get('state')=='compressed':case['thickness_mm']=case['height_mm']
                if v.get('kind')=='spherical-nuts':
                    length=native.run('feature.get',container=ids['blank']['feature'])['parameters']['sides'][0]['length']
                    case['height_mm']=length+case['collar_height_mm']
                case['independent_volume_mm3']=volume(case)
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                assert references()==baseline,'Changed native face reference identity'
                native.run('save');native.run('close');native.run('open',path=str(target));assert references()==baseline
                (folder/'manifest.json').write_text(json.dumps(dict(variants=[case])),encoding='utf-8')
                subprocess.run([str(checker),mode,str(folder)],check=True,stdout=subprocess.DEVNULL)
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(source.read_bytes()).hexdigest()==sha
        results.append(dict(filename=source.name,source_sha256=sha,successive_edits=len(plans),undo_redo=True,save_reopen=True,
            reference_identity=True,independent_volume_and_mass=True,contact_equations=True,locked_unlocked_profile_dragging=True))
        (out/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
        print('Verified '+source.name+f': {len(plans)} repeated edits',flush=True)


if __name__=='__main__':main()
