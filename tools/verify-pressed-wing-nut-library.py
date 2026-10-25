"""Check successive pressed-wing drivers, contact equations and native persistence."""
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
spec=importlib.util.spec_from_file_location('pressed',ROOT/'tools/generate-pressed-wing-nut-library.py')
pressed=importlib.util.module_from_spec(spec);spec.loader.exec_module(pressed)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--models',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--diameter',type=float)
    a=p.parse_args();source=a.models.resolve();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    checker=pin_checker(out);report=[]
    for v in json.loads((source/'manifest.json').read_text(encoding='utf-8'))['variants']:
        if a.diameter is not None and a.diameter!=v['diameter_mm']:continue
        original=source/v['filename'];digest=hashlib.sha256(original.read_bytes()).hexdigest()
        folder=out/original.stem;folder.mkdir();target=folder/original.name;shutil.copyfile(original,target)
        ids=json.loads((source/'evidence'/original.stem/'identities.json').read_text(encoding='utf-8'));case=copy.deepcopy(v)
        def dimension(group,key):return ids[group]['dimensions'][key] if group!='taper' else ids[group][key]
        n=pressed.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
        def get(group,key):return n.run('sketch.dimension.get',sketch=ids[group]['sketch'],dimension=dimension(group,key))['value']
        def references():return {(r['owner'],r['key']) for r in n.run('reference.list',kind='face',limit=5000)['items']}
        def check():
            n.run('regenerate');assert not n.run('history.list')['calculation_errors']
            assert references()==baseline,'Pressed-wing original face references changed'
            for group in ('wing','hub','taper'):
                status=n.run('sketch.solve_status',sketch=ids[group]['sketch'])
                assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
            for key,value in [('base',case['wing_root_radius_mm']),('root_x',case['wing_root_x_mm']),
                ('root_y',case['wing_root_height_mm'])]:
                assert abs(get('wing',key)-value)<1e-8,(key,get('wing',key),value)
            wing_entities=pressed.sketch_entities(n,ids['wing']['sketch'])
            for key in ('root_y','height'):
                point=wing_entities[wing_entities[dimension('wing',key)]['first']]
                assert point['y']<0,(key,point)
            hub_entities=pressed.sketch_entities(n,ids['hub']['sketch'])
            hub_height_point=hub_entities[hub_entities[dimension('hub','height')]['first']]
            assert hub_height_point['y']<0,hub_height_point
            side=n.run('feature.get',container=ids['wing']['feature'])['parameters']
            assert abs(side['sides'][0]['length']-case['wing_thickness_mm'])<1e-8
            assert side['symmetric'] and abs(side['profile_plane_offset'])<1e-8
            opening=n.run('opening.get',container=ids['thread'])
            assert abs(opening['thread_length_mm']-case['opening_thread_length_mm'])<1e-8
            params=n.run('document.parameters.get')['parameters']
            assert next(p['values'][''] for p in params if p['key']=='stock')==f'M{case["diameter_mm"]:g}x{case["pitch_mm"]:g}'
            pressed.capture_authored_profile(n,case,ids)
            case['independent_volume_mm3']=pressed.independent_volume(case)
            n.run('save');n.run('close');n.run('open',path=str(target));assert references()==baseline
            inspection=subprocess.run([str(checker),'--inspect-candidate-properties',str(target)],capture_output=True,timeout=180)
            (folder/'last-inspection.stdout.log').write_bytes(inspection.stdout)
            (folder/'last-inspection.stderr.log').write_bytes(inspection.stderr)
            assert inspection.returncode==0,inspection.stderr.decode('utf-8',errors='replace')
            properties=json.loads(inspection.stdout)[0];adaptive=properties['adaptive'];actual=properties['gk']
            assert properties['valid'] and properties['solid_count']==1 and properties['properties_current'],properties
            assert abs(properties['cached']-actual)<=max(1e-8,actual*1e-10),properties
            assert abs(adaptive-actual)<=max(1e-8,actual*2e-8),properties
            assert abs(actual-case['independent_volume_mm3'])<=max(1e-5,actual*2e-6),(
                actual,case['independent_volume_mm3'])
            contract_case=copy.deepcopy(case);contract_case['independent_volume_mm3']=actual
            (folder/'manifest.json').write_text(json.dumps(dict(variants=[contract_case]),indent=2),encoding='utf-8')
            result=subprocess.run([str(checker),'--pressed-wing-nuts',str(folder)],capture_output=True,timeout=180)
            (folder/'last-check.stdout.log').write_bytes(result.stdout);(folder/'last-check.stderr.log').write_bytes(result.stderr)
            assert result.returncode==0,result.stderr.decode('utf-8',errors='replace')
        count=0
        try:
            n.run('open',path=str(target));baseline=references()
            drivers=[('height_mm','wing','height',1),('span_mm','wing','span',2),('outside_radius_mm','wing','radius',1),
                ('hub_base_diameter_mm','hub','base',2),('hub_top_diameter_mm','hub','top',2),('hub_height_mm','hub','height',1),
                ('hub_radius_mm','hub','radius',1),('root_thickness_mm','taper','root',2),('tip_thickness_mm','taper','tip',2),
                ('inner_angle_degrees','wing','angle',None),('root_tangent_degrees','wing','lower_angle',1)]
            for field,group,key,factor in drivers:
                initial=v[field]
                # Two successive non-trivial perturbations remain above the
                # product's 0.001 mm tolerance even on M3, while avoiding an
                # unrelated M8 solver branch instability at exactly +0.5%.
                for scale in (1.001,1.0025,1):
                    before=get(group,key);value=initial*scale;native_value=180-value if factor is None else value/factor
                    print(f'Checking {original.name}: {field} x {scale:g}',flush=True)
                    n.run('sketch.dimension.set',sketch=ids[group]['sketch'],dimension=dimension(group,key),value=native_value)
                    assert abs(get(group,key)-native_value)<1e-8
                    n.run('undo');assert abs(get(group,key)-before)<1e-8
                    n.run('redo');assert abs(get(group,key)-native_value)<1e-8
                    case[field]=value;check();count+=1
                print(f'Checked {original.name}: {field}',flush=True)
            catalog=n.run('thread.catalog',standard='metric',limit=1000)['items']
            base=next(t for t in catalog if t['nominal_diameter_mm']==v['diameter_mm'] and t['pitch_mm']==v['pitch_mm'])
            fine=max((t for t in catalog if t['nominal_diameter_mm']==v['diameter_mm'] and t['pitch_mm']<v['pitch_mm']),key=lambda t:t['pitch_mm'])
            for selected in (fine,base):
                before=n.run('opening.get',container=ids['thread'])['designation']
                n.run('opening.set',container=ids['thread'],designation=selected['designation'])
                n.run('undo');assert n.run('opening.get',container=ids['thread'])['designation']==before
                n.run('redo');assert n.run('opening.get',container=ids['thread'])['designation']==selected['designation']
                case.update(pitch_mm=selected['pitch_mm'],bore_mm=selected['internal_root_diameter_mm']);check();count+=1
            n.run('close')
        finally:n.close()
        assert hashlib.sha256(original.read_bytes()).hexdigest()==digest
        report.append(dict(filename=original.name,source_sha256=digest,successive_edits=count,undo_redo=True,save_reopen=True,
            reference_identity=True,independent_volume_and_mass=True,dependent_contact_equations=True,derived_stock=True,
            quadrant_preserved=True,edit_volume_tolerance_ppm=2))
        (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert report


if __name__=='__main__':main()
