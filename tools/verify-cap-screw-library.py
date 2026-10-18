"""Check successive cap-screw length edits and the real native drive geometry."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('caps',ROOT/'tools/generate-cap-screw-library.py')
caps=importlib.util.module_from_spec(spec);spec.loader.exec_module(caps)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--standard')
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    p.add_argument('--checker',type=Path,default=ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe');args=p.parse_args()
    source=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((source/'manifest.json').read_text(encoding='utf-8'))['variants'];report=[]
    for v in rows:
        if args.standard and args.standard!=v['standard']:continue
        original=source/v['filename'];digest=hashlib.sha256(original.read_bytes()).hexdigest()
        target=out/v['filename'];assert not target.exists();shutil.copyfile(original,target)
        ids=json.loads((source/'evidence'/original.stem/'identities.json').read_text(encoding='utf-8'))
        native=caps.studs.api.Native(args.cli.resolve(),out/'evidence'/original.stem)
        try:
            native.run('open',path=str(target));references=native.run('reference.list',kind='face')['items']
            def length():return native.run('feature.get',container=ids['shaft'])['parameters']['sides'][0]['length']
            baseline=v['length_mm'];step=max(5,2*v['pitch_mm'])
            for value in (baseline+step,baseline+2*step,baseline):
                before=length();sides=native.run('feature.get',container=ids['shaft'])['parameters']['sides'];sides[0]['length']=value
                native.run('feature.set',container=ids['shaft'],parameters=dict(sides=sides));assert abs(length()-value)<1e-8
                native.run('undo');assert abs(length()-before)<1e-8
                native.run('redo');assert abs(length()-value)<1e-8
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                current=native.run('reference.list',kind='face')['items']
                assert {(r['owner'],r['key']) for r in references}=={(r['owner'],r['key']) for r in current}
                parameters=native.run('document.parameters.get')['parameters']
                stock=next(p['values'][''] for p in parameters if p['key']=='stock')
                assert stock==f'M{v["diameter_mm"]:g}x{value:g}',stock
                native.run('save');native.run('close');native.run('open',path=str(target));assert abs(length()-value)<1e-8
                assert abs(native.run('shaft_thread.get',container=ids['thread'])['length_mm']-v['thread_length_mm'])<1e-8
                case=copy.deepcopy(v);case['length_mm']=value
                (out/'manifest.json').write_text(json.dumps(dict(variants=[case])),encoding='utf-8')
                subprocess.run([str(args.checker.resolve()),'--caps',str(out)],check=True,stdout=subprocess.DEVNULL)
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(original.read_bytes()).hexdigest()==digest
        report.append(dict(filename=v['filename'],source_sha256=digest,successive_edits=3,undo_redo=True,save_reopen=True,
            exact_geometry_after_each_edit=True,thread_references_retained=True))
        (out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print('Verified repeated edits '+original.name,flush=True)
    print(str(len(report))+' cylindrical-head screws, '+str(3*len(report))+' independently checked edits')


if __name__=='__main__':main()
