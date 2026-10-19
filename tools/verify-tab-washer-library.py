"""Verify repeated tab-washer edits against independent native geometry checks."""
import argparse
import copy
import hashlib
import importlib.util
import json
import re
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tabs',ROOT/'tools/generate-tab-washer-library.py')
tabs=importlib.util.module_from_spec(spec);spec.loader.exec_module(tabs)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--filename')
    args=p.parse_args();stage=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    cli=ROOT/'build/cpp-windows-release/zima-cad-cli.exe'
    checker=ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe'
    report=[]
    for v in json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants']:
        if args.filename and args.filename!=v['filename']:continue
        source=stage/v['filename'];digest=hashlib.sha256(source.read_bytes()).hexdigest()
        folder=out/source.stem;folder.mkdir(parents=True,exist_ok=True);target=folder/source.name
        assert not target.exists();shutil.copyfile(source,target)
        ids=json.loads((stage/'evidence'/source.stem/'identities.json').read_text(encoding='utf-8'))
        native=tabs.caps.studs.api.Native(cli,folder/'evidence');case=copy.deepcopy(v);count=0
        try:
            native.run('open',path=str(target))
            def references():return {(r['owner'],r['key']) for r in native.run('reference.list',kind='face')['items']}
            baseline=references()
            for key,dim,initial,multiplier in (
                ('bore_mm','bore',v['bore_mm'],1),
                ('outside_mm','outside',v['outside_mm']/2,2),
                ('root_radius_mm','upper_root',v['root_radius_mm'],1),
                ('tab_width_mm','width',v['tab_width_mm'],1),
                ('tab_length_mm','length',-v['tab_length_mm'],-1),
                ('thickness_mm',None,v['thickness_mm'],1)):
                def get():
                    if dim:return native.run('sketch.dimension.get',sketch=ids['sketch'],dimension=ids['dimensions'][dim])['value']
                    return native.run('feature.get',container=ids['body'])['parameters']['sides'][0]['length']
                def change(value):
                    if dim:native.run('sketch.dimension.set',sketch=ids['sketch'],dimension=ids['dimensions'][dim],value=value)
                    else:
                        sides=native.run('feature.get',container=ids['body'])['parameters']['sides'];sides[0]['length']=value
                        native.run('feature.set',container=ids['body'],parameters=dict(sides=sides))
                for factor in (1.02,1.04,1):
                    before=get();value=initial*factor;change(value);assert abs(get()-value)<1e-8
                    native.run('undo');assert abs(get()-before)<1e-8
                    native.run('redo');assert abs(get()-value)<1e-8
                    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                    assert references()==baseline,'Changed tab-washer face identity'
                    case[key]=value*multiplier
                    parameters=native.run('document.parameters.get')['parameters']
                    stock=next(p['values'][''] for p in parameters if p['key']=='stock')
                    actual=[float(n) for n in re.findall(r'-?\d+(?:\.\d+)?',stock)]
                    expected=[case[k] for k in ('bore_mm','outside_mm','thickness_mm','tab_width_mm','tab_length_mm')]
                    assert len(actual)==len(expected) and all(abs(a-b)<1e-6 for a,b in zip(actual,expected)),stock
                    native.run('save');native.run('close');native.run('open',path=str(target))
                    assert references()==baseline and abs(get()-value)<1e-8
                    (folder/'manifest.json').write_text(json.dumps(dict(variants=[case])),encoding='utf-8')
                    subprocess.run([str(checker),'--tab-washers',str(folder)],check=True,stdout=subprocess.DEVNULL)
                    count+=1
            native.run('close')
        finally:native.close()
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
        report.append(dict(filename=v['filename'],source_sha256=digest,successive_edits=count,
            undo_redo=True,save_reopen=True,reference_identity=True,analytical_volume_after_each_edit=True,derived_stock=True))
        (out/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(f'Checked {source.name}: {count} edits',flush=True)


if __name__=='__main__':main()
