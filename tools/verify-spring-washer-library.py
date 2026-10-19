"""Check repeated bore, outside diameter and thickness edits and persistence."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('washers',ROOT/'tools/generate-spring-washer-library.py')
washers=importlib.util.module_from_spec(spec);spec.loader.exec_module(washers)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--kind',choices=('spring','plain'),default='spring')
    parser.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    parser.add_argument('--checker',type=Path,default=ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe')
    args=parser.parse_args();source=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((source/'manifest.json').read_text(encoding='utf-8'))['variants'];results=[]
    for v in rows:
        original=source/v['filename'];digest=hashlib.sha256(original.read_bytes()).hexdigest()
        target=out/v['filename'];assert not target.exists(),target;shutil.copyfile(original,target)
        ids=json.loads((source/'evidence'/original.stem/'identities.json').read_text(encoding='utf-8'))
        native=washers.caps.studs.api.Native(args.cli.resolve(),out/'evidence'/original.stem)
        try:
            native.run('open',path=str(target))
            def references():return {(r['owner'],r['key']) for r in native.run('reference.list',kind='face')['items']}
            baseline_refs=references()
            def supports():
                if args.kind=='plain':return []
                entities=native.run('sketch.entities',sketch=ids['split_sketch'])['items']
                result=[]
                for item in entities:
                    if item['kind']=='external_reference':
                        e=native.run('sketch.entity.get',sketch=ids['split_sketch'],entity=item['id'])['entity']
                        result.append(tuple(e[k] for k in ('id','source_owner_id','source_semantic_key')))
                return result
            baseline_supports=supports();assert len(baseline_supports)==(2 if args.kind=='spring' else 0)
            def current(dimension):
                if dimension:return native.run('sketch.dimension.get',sketch=ids['sketch'],dimension=dimension)['value']
                return native.run('feature.get',container=ids['annulus'])['parameters']['sides'][0]['length']
            def change(dimension,value):
                if dimension:native.run('sketch.dimension.set',sketch=ids['sketch'],dimension=dimension,value=value)
                else:
                    sides=native.run('feature.get',container=ids['annulus'])['parameters']['sides'];sides[0]['length']=value
                    native.run('feature.set',container=ids['annulus'],parameters=dict(sides=sides))
            cases=[('thickness_mm',None,v['thickness_mm']*f) for f in (1.1,1.2,1)]
            for key,dimension in (('bore_mm',ids['diameters'][1]),('outside_mm',ids['diameters'][0])):
                cases.extend((key,dimension,v[key]+v['width_mm']*f) for f in (.2,-.2,0))
            for key,dimension,value in cases:
                before=current(dimension);change(dimension,value);assert abs(current(dimension)-value)<1e-8
                native.run('undo');assert abs(current(dimension)-before)<1e-8
                native.run('redo');assert abs(current(dimension)-value)<1e-8
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                assert references()==baseline_refs and supports()==baseline_supports,'Changed native source identity'
                case=copy.deepcopy(v);case[key]=value;case['width_mm']=(case['outside_mm']-case['bore_mm'])/2
                params=native.run('document.parameters.get')['parameters']
                stock=next(p['values'][''] for p in params if p['key']=='stock')
                assert stock==f'Ø{case["bore_mm"]:g}x{case["outside_mm"]:g}x{case["thickness_mm"]:g}',stock
                native.run('save');native.run('close');native.run('open',path=str(target))
                assert abs(current(dimension)-value)<1e-8 and references()==baseline_refs and supports()==baseline_supports
                (out/'manifest.json').write_text(json.dumps(dict(variants=[case])),encoding='utf-8')
                subprocess.run([str(args.checker.resolve()),'--'+args.kind+'-washers',str(out)],check=True,stdout=subprocess.DEVNULL)
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(original.read_bytes()).hexdigest()==digest
        results.append(dict(filename=v['filename'],source_sha256=digest,successive_edits=9,
            undo_redo=True,save_reopen=True,reference_identity=True,analytical_volume_after_each_edit=True,derived_stock=True))
        (out/'results.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
        print('Verified repeated bore, outside diameter and thickness edits '+original.name,flush=True)
    print(f'{len(results)} {args.kind} washers; {9*len(results)} independently checked edits',flush=True)


if __name__=='__main__':main()
