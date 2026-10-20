"""Verify native nose-washer edits, sheet dependencies, mass and unfolding."""
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
spec=importlib.util.spec_from_file_location('nose',ROOT/'tools/generate-formed-nose-washer-library.py')
nose=importlib.util.module_from_spec(spec);spec.loader.exec_module(nose)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--models',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--nominal',type=float)
    a=p.parse_args();source=a.models.resolve();out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    checker=pin_checker(out);report=[]
    for original_case in json.loads((source/'manifest.json').read_text(encoding='utf-8'))['variants']:
        if a.nominal is not None and a.nominal!=original_case['nominal_mm']:continue
        v=copy.deepcopy(original_case);original=source/v['filename'];digest=hashlib.sha256(original.read_bytes()).hexdigest()
        folder=out/original.stem;folder.mkdir();target=folder/original.name;shutil.copyfile(original,target)
        ids=json.loads((source/'evidence'/original.stem/'identities.json').read_text(encoding='utf-8'))
        n=nose.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
        def faces():return {(r['owner'],r['key']) for r in n.run('reference.list',kind='face',limit=5000)['items']}
        def get_dimension(group,key):
            return n.run('sketch.dimension.get',sketch=ids[group]['sketch'],dimension=ids[group+'_dimensions'][key])['value']
        def inspect():
            v['independent_volume_mm3']=nose.independent_volume(v);v['bounds_mm']=nose.bounds(v)
            (folder/'manifest.json').write_text(json.dumps(dict(variants=[v]),indent=2),encoding='utf-8')
            result=subprocess.run([str(checker),'--formed-nose-washers',str(folder)],capture_output=True,timeout=120)
            (folder/'last-check.stdout.log').write_bytes(result.stdout);(folder/'last-check.stderr.log').write_bytes(result.stderr)
            assert result.returncode==0,result.stderr.decode('utf-8',errors='replace')
        def check():
            n.run('regenerate');assert not n.run('history.list')['calculation_errors']
            assert faces()==baseline,'Nose washer original face references changed'
            for group in ('base','leg'):
                status=n.run('sketch.solve_status',sketch=ids[group]['sketch'])
                assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
            bend=n.run('bend.get',container=ids['bend']['container']);leg=n.run('flat.get',container=ids['leg']['container'])
            assert abs(bend['radius_mm']-v['thickness_mm'])<1e-8 and bend['radius_follows_thickness']
            assert abs(bend['thickness_mm']-v['thickness_mm'])<1e-8 and abs(leg['thickness_mm']-v['thickness_mm'])<1e-8
            assert abs(get_dimension('leg','width')-v['nose_width_mm'])<1e-8
            stock=next(p['values'][''] for p in n.run('document.parameters.get')['parameters'] if p['key']=='stock')
            assert abs(float(stock.split(';h=')[1])-(v['thickness_mm']-nose.bounds(v)[2]))<1e-5,stock
            n.run('save');n.run('close');n.run('open',path=str(target));assert faces()==baseline
            inspect()
        count=0
        try:
            n.run('open',path=str(target));baseline=faces();check()
            for field,group,key,factor in [('outside_mm','base','outside',2),('bore_mm','base','bore',1),
                ('nose_width_mm','base','nose_width',1),('root_distance_mm','base','root',1),('leg_length_mm','leg','length',1)]:
                for scale in (1.01,1.02,1):
                    before=get_dimension(group,key);value=original_case[field]*scale/factor
                    n.run('sketch.dimension.set',sketch=ids[group]['sketch'],dimension=ids[group+'_dimensions'][key],value=value)
                    assert abs(get_dimension(group,key)-value)<1e-8
                    n.run('undo');assert abs(get_dimension(group,key)-before)<1e-8
                    n.run('redo');assert abs(get_dimension(group,key)-value)<1e-8
                    v[field]=original_case[field]*scale;check();count+=1
                print(original.name+': '+field,flush=True)
            for scale in (1.01,1.02,1):
                before=n.run('flat.get',container=ids['base']['container'])['thickness_mm'];value=original_case['thickness_mm']*scale
                n.run('flat.set',container=ids['base']['container'],thickness_mm=value)
                n.run('undo');assert abs(n.run('flat.get',container=ids['base']['container'])['thickness_mm']-before)<1e-8
                n.run('redo');v['thickness_mm']=value;check();count+=1
            for angle in (80,85,90):
                before=n.run('bend.get',container=ids['bend']['container'])['angle_degrees']
                n.run('bend.set',container=ids['bend']['container'],angle_degrees=angle)
                n.run('undo');assert abs(n.run('bend.get',container=ids['bend']['container'])['angle_degrees']-before)<1e-8
                n.run('redo');v['angle_degrees']=angle;check();count+=1
            before=n.run('body_properties.create')['volume_mm3'];n.run('undo')
            n.run('unbend.create',all=True);assert not n.run('history.list')['calculation_errors']
            unfolded=n.run('body_properties.create')['volume_mm3'];n.run('undo')
            assert abs(before-unfolded)<before*1e-8,(before,unfolded)
            n.run('undo');check()
            n.run('close')
        finally:n.close()
        assert hashlib.sha256(original.read_bytes()).hexdigest()==digest,'Verification changed its source'
        report.append(dict(filename=original.name,source_sha256=digest,successive_edits=count,undo_redo=True,save_reopen=True,
            reference_identity=True,independent_volume_and_mass=True,dependent_sheet_thickness=True,derived_stock=True,
            real_bend=True,unfolding_volume=True,locked_unlocked_profile_dragging=True))
        (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert report


if __name__=='__main__':main()
