"""Verify contact and clearance in disposable native seating assemblies."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import subprocess
from fastener_verification_runtime import pin_checker

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('nose',ROOT/'tools/generate-formed-nose-washer-library.py')
nose=importlib.util.module_from_spec(spec);spec.loader.exec_module(nose)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--models',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    checker=pin_checker(out);results=[]
    for v in json.loads((a.models/'manifest.json').read_text(encoding='utf-8'))['variants']:
        source=(a.models/v['filename']).resolve();digest=hashlib.sha256(source.read_bytes()).hexdigest()
        folder=out/source.stem;folder.mkdir();host=folder/'test-host.prtz';assembly=folder/'seating.asmz'
        native=nose.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence',
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        # Deliberate test clearance, not a claim about the standard's dz/hz.
        t=v['thickness_mm'];f=v['nose_width_mm'];R=v['outside_mm']/2;depth=v['height_mm']+2*t
        radius=math.hypot(t,f/2)+.1
        try:
            native.run('new',type='part',name='Seating test host')
            sk=native.run('sketch.create',name='Test plate',plane='XY')['sketch']
            corners=[[-2*R,-2*R],[2*R,-2*R],[2*R,2*R],[-2*R,2*R]]
            for i,p0 in enumerate(corners):native.run('sketch.segment.create',sketch=sk,first=p0,second=corners[(i+1)%4],snap_mm=1e-8)
            native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,sides=[nose.caps.side(operation='none'),nose.caps.side(depth)]))
            sk=native.run('sketch.create',name='Test clearance holes',plane='XY')['sketch']
            for center,r in [([0,0],v['bore_mm']/2),([v['root_distance_mm']+t,0],radius)]:
                native.run('sketch.circle.create',sketch=sk,center=center,radius_mm=r)
            native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(symmetric=False,sides=[nose.caps.side(operation='none'),nose.caps.side(depth)]))
            native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value='Steel'),
                dict(key='MASS_DENSITY',value='7.85e-6',unit='kg/mm^3',descriptions=dict(zip(nose.caps.LANGUAGES,
                    ('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
            native.run('document.relations.set',relations='mass = model.mass\n')
            native.run('regenerate');assert not native.run('history.list')['calculation_errors']
            assert float(next(p['values'][''] for p in native.run('document.parameters.get')['parameters'] if p['key']=='mass'))>0
            native.run('save_as',path=str(host));native.run('close',discard=True)
            host_id=next(d['id'] for d in native.run('open',path=str(host)) if d['active'])
            washer_id=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
            native.run('new',type='assembly',name='Nose washer seating')
            native.run('component.insert',source=host_id);native.run('component.insert',source=washer_id)
            native.run('regenerate');native.run('save_as',path=str(assembly));native.run('close',discard=True)
            native.run('open',path=str(assembly));components=native.run('component.list')['items'];assert len(components)==2
            paths=[]
            for item in components:
                assert all(abs(x)<1e-9 for x in item['placement'].values()),item['placement']
                path=Path(item['source_path']);paths.append(path if path.is_absolute() else assembly.parent/path)
            checked=subprocess.run([str(checker),'--verify-native-seating',*[str(p) for p in paths]],capture_output=True,timeout=120)
            (folder/'seating.stdout.log').write_bytes(checked.stdout);(folder/'seating.stderr.log').write_bytes(checked.stderr)
            assert checked.returncode==0,checked.stderr.decode('utf-8',errors='replace')
            results.append(dict(filename=source.name,source_sha256=digest,native_assembly=str(assembly),
                selected_clearance_hole_diameter_mm=2*radius,standard_hole_fit_certified=False,
                save_reopen=True,**json.loads(checked.stdout)))
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
        (out/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
        print('Native seating assembly passed: '+source.name,flush=True)


if __name__=='__main__':main()
