"""Install verified fitted bolts and native illustrations after complete preflight."""
import argparse
import configparser
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
LIBRARY=ROOT/'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS/02-FITTED'
LANGUAGES=('cs','en','de','fr','ru')
FIELDS=('name','standard','stock','fit','material','quantity','mass')
spec=importlib.util.spec_from_file_location('installer',ROOT/'tools/install-fastener-library.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def native(path):
    parser=configparser.RawConfigParser();parser.read(path,encoding='utf-8');return parser


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('models','thumbnails','references','verification'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    variants=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']
    verified={v['filename']:v for v in json.loads(args.verification.read_text(encoding='utf-8'))}
    illustrations=json.loads((args.references/'illustrations.json').read_text(encoding='utf-8'))
    assert len(variants)==len(verified)==24 and len(illustrations)==2
    index=LIBRARY/'0000-index';copies=[];identities=set();names=set()
    changes={'Directory':{'Parameters':', '.join(FIELDS),'AutoIndex':'true'},'Parameters':{},'Parts':{},'Files':{}}
    for variant in variants:
        source=args.models/variant['filename'];case=verified[source.name]
        assert case['source_sha256']==digest(source)
        assert all(case[k] for k in ('undo_redo','save_reopen','exact_geometry_after_each_edit','thread_references_retained'))
        document=native(source);identity=document['Document']['document_id']
        assert identity not in identities and source.name.casefold() not in names
        identities.add(identity);names.add(source.name.casefold())
        values=json.loads(document['UserParameterValues']['data'])
        assert set(values['name'])==set(LANGUAGES) and values['fit']['']=='n6' and 'product_grade' not in values
        assert float(values['mass'][''])>0
        image=args.thumbnails/(source.name+'.png')
        with Image.open(image) as bitmap:assert bitmap.size==(320,240);bitmap.verify()
        copies.extend(((source,LIBRARY/source.name),(image,index/'thumbnails'/image.name)))
        for key in FIELDS:
            for language in LANGUAGES:
                changes['Parameters'][key+'\\Label\\'+language]=document['UserParameterLabels'][key+'\\'+language]
            for language,value in values[key].items():
                changes['Parts'][source.stem+'\\'+key+('\\'+language if language else '')]=value
        changes['Files'].update({source.stem+'\\Path':source.name,source.stem+'\\Provider':'zima-cad',
            source.stem+'\\Thumbnail':'0000-index/thumbnails/'+image.name})
    for row in illustrations:
        folder=args.references/row['standard'].replace('ČSN','CSN').replace(' ','')
        part=folder/row['filename'];drawing=folder/row['drawing']
        identity=native(part)['Document']['document_id'];assert identity not in identities;identities.add(identity)
        data=json.loads(native(drawing)['Document']['param.cpp_drawing'])
        assert data['source_path']==part.name and all(v['source_path']==part.name for s in data['sheets'] for v in s['views'])
        assert row['languages']==list(LANGUAGES)
        for path in (part,drawing,part.with_suffix('.png'),*(folder/(part.stem+'_'+lang+'.png') for lang in LANGUAGES)):
            assert path.is_file();copies.append((path,index/path.name))
    for source,target in copies:
        assert target.resolve().is_relative_to(LIBRARY.resolve()) and not target.exists(),target
    # Every model, metadata field and illustration passes before installation.
    for source,target in copies:
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    installer.update_ini(index/'metadata.ini',changes)
    metadata=(index/'metadata.ini').read_text(encoding='utf-8')
    metadata=metadata.replace('Parameters="'+', '.join(FIELDS)+'"','Parameters='+', '.join(FIELDS))
    (index/'metadata.ini').write_text(metadata,encoding='utf-8')
    receipt=[dict(file=p.relative_to(ROOT).as_posix(),sha256=digest(p)) for _,p in copies]
    (args.models/'installed.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    print('Installed 24 fitted bolts, horizontal thumbnails and two five-language native illustrations')


if __name__=='__main__':main()
