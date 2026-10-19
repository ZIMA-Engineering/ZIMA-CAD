"""Verify staged knurled nuts with successive native edits and exact sections."""
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
spec=importlib.util.spec_from_file_location('knurl',ROOT/'tools/generate-knurled-nut-library.py')
knurl=importlib.util.module_from_spec(spec);spec.loader.exec_module(knurl)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    stage=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);report=[]
    checker=pin_checker(out)
    for v in json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants']:
        source=stage/v['filename'];digest=hashlib.sha256(source.read_bytes()).hexdigest()
        folder=out/source.stem;folder.mkdir(parents=True,exist_ok=True);target=folder/source.name
        assert not target.exists();shutil.copyfile(source,target)
        ids=json.loads((stage/'evidence'/source.stem/'identities.json').read_text(encoding='utf-8'))
        native=knurl.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
        case=copy.deepcopy(v);edits=0
        try:
            native.run('open',path=str(target))
            def references():
                return {(r['owner'],r['key']) for owner in (ids['blank']['feature'],ids['thread'])
                    for r in native.run('reference.list',kind='face',owner=owner,limit=5000)['items']}
            baseline=references()
            def check():
                assert not native.run('history.list')['calculation_errors']
                assert references()==baseline,'Changed original blank/thread face identities'
                opening=native.run('opening.get',container=ids['thread'])
                for key in ('thread_length_mm','bore_length_mm'):assert abs(opening[key]-case['height_mm'])<1e-8
                stock=next(p['values'][''] for p in native.run('document.parameters.get')['parameters'] if p['key']=='stock')
                assert stock==f'M{case["diameter_mm"]:g}x{case["pitch_mm"]:g}',stock
                native.run('save');native.run('close');native.run('open',path=str(target))
                assert references()==baseline
                case['independent_volume_mm3']=knurl.independent.volume(case)
                (folder/'manifest.json').write_text(json.dumps(dict(variants=[case]),indent=2),encoding='utf-8')
                subprocess.run([str(checker),'--knurled-nuts',str(folder)],check=True,stdout=subprocess.DEVNULL)
                # Dispose only numbered save backups authored in this isolated
                # verification folder; preserve the final document and receipts.
                for backup in folder.glob(target.name+'.*'):
                    if backup.name[len(target.name)+1:].isdigit():
                        assert backup.resolve().parent==folder
                        backup.unlink()
            check()
            drivers=[('height_mm',ids['blank'],'height',1),('outside_mm',ids['blank'],'outside',.5),
                ('root_radius_mm',ids['blank'],'radius',1),('knurl_pitch_mm',ids['groove'],'upper',.5)]
            if not v['low']:drivers.append(('head_height_mm',ids['blank'],'head_height',1))
            for key,item,dimension,factor in drivers:
                for scale in (1.02,1.04,1):
                    value=v[key]*scale*factor
                    read=lambda:native.run('sketch.dimension.get',sketch=item['sketch'],dimension=item[dimension])['value']
                    before=read()
                    native.run('sketch.dimension.set',sketch=item['sketch'],dimension=item[dimension],value=value)
                    assert abs(read()-value)<1e-8
                    native.run('undo');assert abs(read()-before)<1e-8
                    native.run('redo');assert abs(read()-value)<1e-8
                    native.run('regenerate');case[key]=value/factor
                    if v['low']:case['head_height_mm']=case['height_mm']-case['root_radius_mm']
                    check();edits+=1
                    print(f'Checked {source.name}: {key}={case[key]:g}',flush=True)
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
        report.append(dict(filename=source.name,source_sha256=digest,successive_edits=edits,
            independent_volume_and_mass=True,real_knurl_flanks=True,thread_clearance=True,
            origin_seating=True,locked_native_dragging=True,undo_redo=True,save_reopen=True,
            original_blank_thread_face_identity=True,gui_verified=False))
        (out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')


if __name__=='__main__':main()
