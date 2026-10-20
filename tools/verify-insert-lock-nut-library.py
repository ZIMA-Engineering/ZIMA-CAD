"""Verify dependent edits of homogeneous steel lock-nut Parts on private copies."""
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
spec=importlib.util.spec_from_file_location('nuts',ROOT/'tools/generate-insert-lock-nut-library.py')
nuts=importlib.util.module_from_spec(spec);spec.loader.exec_module(nuts)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--models',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--diameter',type=float);args=p.parse_args()
    source=args.models.resolve();out=args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    checker=pin_checker(out);report=[]
    for v in json.loads((source/'manifest.json').read_text(encoding='utf-8'))['variants']:
        if args.diameter is not None and args.diameter!=v['diameter_mm']:continue
        original=source/v['filename'];digest=hashlib.sha256(original.read_bytes()).hexdigest()
        folder=out/original.stem;folder.mkdir();target=folder/original.name;shutil.copyfile(original,target)
        ids=json.loads((source/'evidence'/original.stem/'identities.json').read_text(encoding='utf-8'))
        shell,insert=ids['shell'],ids['insert'];collar=shell['collar'];case=copy.deepcopy(v)
        native=nuts.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
        def dim_get(sketch,dimension):return native.run('sketch.dimension.get',sketch=sketch,dimension=dimension)['value']
        def collar_get(key):return dim_get(collar['sketch'],collar['dimensions'][key])
        def feature_get(feature,key):return native.run('feature.get',container=feature)['parameters'][key]
        def extent_set(feature,value):
            sides=feature_get(feature,'sides');sides[0]['length']=value
            native.run('feature.set',container=feature,parameters=dict(sides=sides))
        def references():return {(r['owner'],r['key']) for r in native.run('reference.list',kind='face',limit=5000)['items']}
        def check():
            native.run('regenerate');assert not native.run('history.list')['calculation_errors']
            assert references()==baseline,'Changed persisted original face references'
            case['shell_volume_mm3']=nuts.independent_shell_volume(case)
            height=case['height_mm']-case['lip_mm']-case['hex_height_mm']
            case['insert_volume_mm3']=math.pi*((case['collar_outside_radius_mm']-case['collar_wall_mm'])**2-case['insert_bore_radius_mm']**2)*height
            expected={'outside':case['collar_outside_radius_mm'],'cavity':case['collar_outside_radius_mm']-case['collar_wall_mm'],
                'lip_bore':case['lip_bore_radius_mm'],'floor':-case['hex_height_mm'],'top':-case['height_mm'],
                'lip_floor':-case['height_mm']+case['lip_mm'],'round':case['corner_radius_mm']}
            for key,value in expected.items():assert abs(collar_get(key)-value)<1e-8,(key,collar_get(key),value)
            for key,value in [('outside',2*expected['cavity']),('inside',2*case['insert_bore_radius_mm'])]:
                assert abs(dim_get(insert['sketch'],insert['dimensions'][key])-value)<1e-8,(key,value)
            assert abs(feature_get(insert['feature'],'profile_plane_offset')-case['hex_height_mm'])<1e-8
            assert abs(feature_get(insert['feature'],'sides')[0]['length']-height)<1e-8
            thread=native.run('opening.get',container=shell['thread'])
            assert abs(thread['thread_length_mm']-case['hex_height_mm'])<1e-8
            assert abs(thread['chamfer_depth_mm']-case['mouth_depth_mm'])<1e-8
            for sk in (shell['hexagon'],collar['sketch'],insert['sketch'],*[b['sketch'] for b in shell['bevels']]):
                status=native.run('sketch.solve_status',sketch=sk)
                assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
            params=native.run('document.parameters.get')['parameters']
            assert next(row['values'][''] for row in params if row['key']=='stock')==f'M{case["diameter_mm"]:g}x{case["pitch_mm"]:g}'
            native.run('save');native.run('close');native.run('open',path=str(target))
            assert references()==baseline
            (folder/'manifest.json').write_text(json.dumps(dict(variants=[case]),indent=2),encoding='utf-8')
            result=subprocess.run([str(checker),'--insert-lock-nuts',str(folder)],capture_output=True,timeout=120)
            (folder/'last-check.stdout.log').write_bytes(result.stdout);(folder/'last-check.stderr.log').write_bytes(result.stderr)
            assert result.returncode==0,result.stderr.decode('utf-8',errors='replace')
        count=0
        try:
            native.run('open',path=str(target));baseline=references()
            # Each driver is changed twice and restored, with Undo/Redo before
            # each regeneration and independent geometric checks after reopen.
            for key,initial in [('hex_height_mm',v['hex_height_mm']),('across_flats_mm',v['across_flats_mm']),('height_mm',v['height_mm'])]:
                for factor in (1.02,1.04,1):
                    value=initial*factor
                    if key=='hex_height_mm':
                        get=lambda:feature_get(shell['prism'],'sides')[0]['length']
                        change=lambda x:extent_set(shell['prism'],x)
                    elif key=='across_flats_mm':
                        get=lambda:dim_get(shell['hexagon'],shell['size'])*math.sqrt(3)/2
                        change=lambda x:native.run('sketch.dimension.set',sketch=shell['hexagon'],dimension=shell['size'],value=2*x/math.sqrt(3))
                    else:
                        get=lambda:-collar_get('top')
                        change=lambda x:native.run('sketch.dimension.set',sketch=collar['sketch'],dimension=collar['dimensions']['top'],value=-x)
                    before=get();change(value);assert abs(get()-value)<1e-8
                    native.run('undo');assert abs(get()-before)<1e-8
                    native.run('redo');assert abs(get()-value)<1e-8
                    case[key]=value
                    if key=='across_flats_mm':case['collar_outside_radius_mm']=.46*value
                    check();count+=1
            catalog=native.run('thread.catalog',standard='metric',limit=1000)['items']
            base=next(t for t in catalog if t['nominal_diameter_mm']==v['diameter_mm'] and t['pitch_mm']==v['pitch_mm'])
            alternatives=[t for t in catalog if t['nominal_diameter_mm']==v['diameter_mm'] and t['pitch_mm']<v['pitch_mm']]
            edits=[max(alternatives,key=lambda t:t['pitch_mm']),base] if alternatives else []
            for selected in edits:
                before=native.run('opening.get',container=shell['thread'])['designation']
                native.run('opening.set',container=shell['thread'],designation=selected['designation'])
                native.run('undo');assert native.run('opening.get',container=shell['thread'])['designation']==before
                native.run('redo');assert native.run('opening.get',container=shell['thread'])['designation']==selected['designation']
                case.update(pitch_mm=selected['pitch_mm'],bore_mm=selected['internal_root_diameter_mm'])
                pitch=case['pitch_mm'];d=case['diameter_mm']
                case.update(collar_wall_mm=.3*pitch,lip_mm=.2*pitch,corner_radius_mm=.125*pitch,
                    lip_bore_radius_mm=d/2+.15*pitch,insert_bore_radius_mm=d/2-.1*pitch)
                check();count+=1
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(original.read_bytes()).hexdigest()==digest
        report.append(dict(filename=original.name,source_sha256=digest,successive_edits=count,undo_redo=True,
            save_reopen=True,original_reference_identity=True,dependent_insert=True,independent_brep_volume=True))
        (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(f'Checked {original.name}: {count} successive edits',flush=True)
    assert report


if __name__=='__main__':main()
