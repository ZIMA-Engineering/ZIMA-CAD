"""Install separately verified stud/cylindrical-head candidates and native art.

All source digests, edit receipts, metadata and images pass before any copy.
Existing exact files may be verified again. Replacement requires an explicit
receipt matching the unchanged previous installation.
"""
import argparse
import configparser
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS'
LANGUAGES=('cs','en','de','fr','ru')
FIELDS=('name','standard','stock','material','quantity','mass')
spec=importlib.util.spec_from_file_location('installer',ROOT/'tools/install-fastener-library.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)


def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def read(p):
    c=configparser.RawConfigParser();c.optionxform=str;c.read(p,encoding='utf-8');return c


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--kind',choices=('studs','caps'),required=True)
    p.add_argument('--replace-receipt',type=Path,help='Explicitly replace only unchanged files from a previous installation receipt')
    for name in ('models','verification','thumbnails','references'):p.add_argument('--'+name,type=Path,required=True)
    args=p.parse_args();rows=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']
    cases={v['filename']:v for v in json.loads(args.verification.read_text(encoding='utf-8'))}
    assert len(rows)==len(cases)==(74 if args.kind=='studs' else 40)
    references=json.loads((args.references/'illustrations.json').read_text(encoding='utf-8'));assert len(references)==3
    previous={}
    if args.replace_receipt:
        previous={row['path']:row['sha256'] for row in json.loads(args.replace_receipt.read_text(encoding='utf-8'))}
    changes={};copies=[];identities=set();families={}
    for v in rows:
        lib=BASE/('03-STUDS' if args.kind=='studs' else '04-CAP-HEAD/'+v['family']);index=lib/'0000-index'
        changes.setdefault(index,{'Directory':{'Parameters':', '.join(FIELDS),'AutoIndex':'true'},'Parameters':{},'Parts':{},'Files':{}})
        update=changes[index];source=args.models/v['filename'];case=cases[source.name]
        assert digest(source)==case['source_sha256'] and case['successive_edits']==3
        assert all(case[k] for k in ('undo_redo','save_reopen','exact_geometry_after_each_edit','thread_references_retained'))
        document=read(source);identity=document['Document']['document_id'];assert identity not in identities;identities.add(identity)
        values=json.loads(document['UserParameterValues']['Data']);assert float(values['mass'][''])>0
        assert set(values['name'])==set(LANGUAGES)
        image=args.thumbnails/(source.name+'.png')
        with Image.open(image) as bitmap:assert bitmap.size==(320,240);bitmap.verify()
        copies.extend(((source,lib/source.name),(image,index/'thumbnails'/image.name)))
        for key in FIELDS:
            for lang in LANGUAGES:update['Parameters'][key+'\\Label\\'+lang]=document['UserParameterLabels'][key+'\\'+lang]
            for lang,value in values[key].items():update['Parts'][source.stem+'\\'+key+('\\'+lang if lang else '')]=value
        update['Files'].update({source.stem+'\\Path':source.name,source.stem+'\\Provider':'zima-cad',source.stem+'\\Thumbnail':'0000-index/thumbnails/'+image.name})
        families[v['standard']]=index
    for row in references:
        folder=args.references/row['standard'].replace('ČSN','CSN').replace(' ','');index=families[row['standard']]
        part=folder/row['part'];drawing=folder/row['drawing'];identity=read(part)['Document']['document_id']
        assert identity not in identities;identities.add(identity)
        definition=json.loads(read(drawing)['Document']['param.cpp_drawing'])
        assert definition['source_path']==part.name
        assert all(v['source_path']==part.name for s in definition['sheets'] for v in s['views'])
        for path in [part,drawing,part.with_suffix('.png'),*(folder/(part.stem+'_'+lang+'.png') for lang in LANGUAGES)]:
            assert path.is_file();copies.append((path,index/path.name))
    for source,target in copies:
        assert target.resolve().is_relative_to(BASE.resolve())
        if target.exists() and digest(target)!=digest(source):
            assert previous.get(target.relative_to(ROOT).as_posix())==digest(target),'Different existing asset: '+str(target)
    for source,target in copies:
        if not target.exists() or digest(target)!=digest(source):target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
    for index,update in changes.items():
        metadata=index/'metadata.ini';installer.update_ini(metadata,update)
        metadata.write_text(metadata.read_text(encoding='utf-8').replace('Parameters="'+', '.join(FIELDS)+'"','Parameters='+', '.join(FIELDS)),encoding='utf-8')
    receipt=[dict(path=p.relative_to(ROOT).as_posix(),sha256=digest(p)) for _,p in copies]
    (args.models/'installed.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    print('Installed or verified '+str(len(rows))+' native screws with metadata, thumbnails and three localized native Drawing pairs')


if __name__=='__main__':main()
