"""Stage geometry-linked socket entrances without overwriting factory inputs."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/file)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
caps=module('caps','generate-cap-screw-library.py')
buttons=module('buttons','generate-button-screw-library.py')
special=module('special','generate-special-cap-screw-library.py')
BASE=ROOT/'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for field in ('stage','base-caps-stage','base-buttons-stage','special-stage'):p.add_argument('--'+field,type=Path,required=True)
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe');args=p.parse_args()
    out=args.stage.resolve();out.mkdir(parents=True,exist_ok=True)
    groups=[(caps.variants(),args.base_caps_stage,True),(buttons.variants(),args.base_buttons_stage,True),(special.variants(),args.special_stage,False)]
    rows=[v for values,_,_ in groups for v in values]
    (out/'manifest.json').write_text(json.dumps(dict(variants=rows),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    receipts=[]
    for values,prior,factory in groups:
        for v in values:
            category='06-PAN-HEAD' if v['family']=='04-BUTTON-A2' else '04-CAP-HEAD'
            original=(BASE/category/v['family'] if factory else prior)/v['filename'];target=out/v['filename']
            assert original.is_file() and not target.exists(),original
            digest=hashlib.sha256(original.read_bytes()).hexdigest();shutil.copyfile(original,target)
            ids=json.loads((prior/'evidence'/original.stem/'identities.json').read_text(encoding='utf-8'))
            evidence=out/'evidence'/original.stem;evidence.mkdir(parents=True,exist_ok=True)
            native=caps.studs.api.Native(args.cli.resolve(),evidence)
            try:
                native.run('open',path=str(target))
                if v['socket'] and not v.get('socket_no') and (factory or not ids.get('entry')):
                    ids['entry']=caps.socket_entry(native,v,ids['opening'],ids.get('entry'))
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                native.run('save');native.run('close');native.run('open',path=str(target));native.run('regenerate')
                assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')
            finally:native.close()
            (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
            assert hashlib.sha256(original.read_bytes()).hexdigest()==digest
            receipts.append(dict(filename=v['filename'],source=str(original),source_sha256=digest,staged_sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
            (out/'source-receipts.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')
            print('Staged '+target.name,flush=True)


if __name__=='__main__':main()
