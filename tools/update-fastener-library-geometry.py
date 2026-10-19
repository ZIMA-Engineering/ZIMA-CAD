"""Update current factory stud/socket definitions while retaining authored IDs."""
import argparse
import configparser
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('caps',ROOT/'tools/generate-cap-screw-library.py')
caps=importlib.util.module_from_spec(spec);spec.loader.exec_module(caps)


def identity(path):
    data=configparser.RawConfigParser();data.read(path,encoding='utf-8')
    return data['Document']['document_id'],{section for section in data.sections() if section.startswith(('Container.','Sketch.','Entity.'))}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--kind',choices=('studs','caps'),required=True)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe');args=p.parse_args()
    source=args.source.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=caps.studs.variants() if args.kind=='studs' else caps.variants()
    (out/'manifest.json').write_text(json.dumps(dict(variants=rows),ensure_ascii=False,indent=2),encoding='utf-8')
    receipts=[]
    for v in rows:
        original=source/v['filename'];target=out/original.name;assert not target.exists()
        digest=hashlib.sha256(original.read_bytes()).hexdigest();before=identity(original)
        ids=json.loads((source/'evidence'/original.stem/'identities.json').read_text(encoding='utf-8'))
        shutil.copyfile(original,target)
        evidence=out/'evidence'/target.stem;evidence.mkdir(parents=True,exist_ok=True)
        if args.kind=='studs' or v['standard']=='ISO 4762':
            native=caps.studs.api.Native(args.cli.resolve(),evidence)
            try:
                native.run('open',path=str(target))
                if args.kind=='studs':
                    sides=native.run('feature.get',container=ids['shaft'])['parameters']['sides']
                    sides[0]['length']=v['metal_length_mm'];sides[1]['length']=v['length_mm']
                    native.run('feature.set',container=ids['shaft'],parameters=dict(symmetric=False,sides=sides))
                    source_faces=caps.studs.faces(native,ids['shaft']);cones=caps.studs.faces(native,ids['chamfer'])
                    for i,sign in enumerate((1,-1)):
                        end=v['metal_length_mm'] if sign>0 else -v['length_mm']
                        cylinder=next(r for r,s in source_faces if s and s['kind']=='cylinder' and s['axis'][2]*sign<0)
                        start=next(r for r,s in source_faces if s and s['kind']=='plane' and abs(s['origin'][2]-end)<1e-6)
                        cone=next(r for r,s in cones if s and s['kind']=='cone' and s['origin'][2]*sign>0)
                        native.run('shaft_thread.set',container=ids['threads'][i],cylinder=caps.studs.ref(cylinder),start=caps.studs.ref(start),chamfer=caps.studs.ref(cone))
                    native.run('save');caps.studs.relations(native,v,target,ids)
                else:
                    native.run('feature.set',container=ids['opening'],parameters=dict(profile_plane_offset=-(v['head_height_mm']-v['drive_depth_mm'])))
                    ids['entry']=caps.socket_entry(native,v,ids['opening'])
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                native.run('save');native.run('close');native.run('open',path=str(target));native.run('regenerate')
                assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')
            finally:native.close()
        after=identity(target);assert before[0]==after[0] and before[1]<=after[1],target
        assert hashlib.sha256(original.read_bytes()).hexdigest()==digest
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        receipts.append(dict(filename=target.name,original_sha256=digest,document_id_retained=True,original_authored_ids_retained=True))
        (out/'identity-preservation.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')
        print('Updated with retained identity '+target.name,flush=True)


if __name__=='__main__':main()
