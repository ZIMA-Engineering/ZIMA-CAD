"""Verify native thread-catalog edits without changing authored knurled nuts."""
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
spec=importlib.util.spec_from_file_location('knurl',ROOT/'tools/generate-knurled-nut-library.py')
knurl=importlib.util.module_from_spec(spec);spec.loader.exec_module(knurl)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants'];report=[]
    checker=pin_checker(out)
    for variant in rows:
        source=args.models/variant['filename'];digest=hashlib.sha256(source.read_bytes()).hexdigest()
        folder=out/source.stem;folder.mkdir(parents=True,exist_ok=True);target=folder/source.name
        assert not target.exists();shutil.copyfile(source,target)
        ids=json.loads((args.models/'evidence'/source.stem/'identities.json').read_text(encoding='utf-8'))
        native=knurl.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
        edits=0
        try:
            native.run('open',path=str(target))
            def references():
                return {(r['owner'],r['key']) for owner in (ids['blank']['feature'],ids['thread'])
                    for r in native.run('reference.list',kind='face',owner=owner,limit=5000)['items']}
            baseline=references()
            catalog=native.run('thread.catalog',standard='metric',limit=1000)['items']
            base=next(t for t in catalog if abs(t['nominal_diameter_mm']-variant['diameter_mm'])<1e-8
                and abs(t['pitch_mm']-variant['pitch_mm'])<1e-8)
            # Retain the authored mouth chamfer and clearance. Only exercise
            # alternatives that leave real material and do not exceed that bore.
            limit=variant['clearance_diameter_mm'] if not variant['low'] else variant['hub_diameter_mm']*.95
            compatible=[t for t in catalog if t['internal_root_diameter_mm']+2*variant['mouth_chamfer_mm']<=limit+1e-8]
            same=[t for t in compatible if t['designation']!=base['designation'] and
                abs(t['nominal_diameter_mm']-variant['diameter_mm'])<1e-8]
            other=[t for t in compatible if abs(t['nominal_diameter_mm']-variant['diameter_mm'])>1e-8]
            selections=same[:2]
            if other:selections.append(min(other,key=lambda t:abs(t['nominal_diameter_mm']-variant['diameter_mm'])))
            assert selections,'No feasible alternative catalog entry'
            selections.append(base)
            for selected in selections:
                before=native.run('opening.get',container=ids['thread'])['designation']
                native.run('opening.set',container=ids['thread'],designation=selected['designation'])
                native.run('undo');assert native.run('opening.get',container=ids['thread'])['designation']==before
                native.run('redo');assert native.run('opening.get',container=ids['thread'])['designation']==selected['designation']
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                assert references()==baseline
                case=copy.deepcopy(variant)
                case.update(diameter_mm=selected['nominal_diameter_mm'],pitch_mm=selected['pitch_mm'],bore_mm=selected['internal_root_diameter_mm'])
                case['independent_volume_mm3']=knurl.independent.volume(case)
                stock=next(p['values'][''] for p in native.run('document.parameters.get')['parameters'] if p['key']=='stock')
                assert stock==f'M{case["diameter_mm"]:g}x{case["pitch_mm"]:g}',stock
                opening=native.run('opening.get',container=ids['thread'])
                assert all(abs(opening[k]-case['height_mm'])<1e-8 for k in ('thread_length_mm','bore_length_mm'))
                native.run('save');native.run('close');native.run('open',path=str(target))
                assert references()==baseline
                (folder/'manifest.json').write_text(json.dumps(dict(variants=[case]),indent=2),encoding='utf-8')
                subprocess.run([str(checker),'--knurled-nuts',str(folder)],check=True,stdout=subprocess.DEVNULL)
                for backup in folder.glob(target.name+'.*'):
                    if backup.name[len(target.name)+1:].isdigit():
                        assert backup.resolve().parent==folder;backup.unlink()
                edits+=1;print(f'Checked {source.name}: thread {selected["designation"]}',flush=True)
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
        report.append(dict(filename=source.name,source_sha256=digest,catalog_edits=edits,
            independent_volume_and_mass=True,reference_identity=True,derived_stock=True,
            thread_follows_height=True,undo_redo=True,save_reopen=True))
        (out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')


if __name__=='__main__':main()
