"""Promote native-verified, metadata-only factory consolidation and approved artwork.

Preflight all old Git-owned paths before replacing any file. User-authored
documents and unrelated metadata rows are deliberately outside this operation.
"""
import argparse
import configparser
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
LIBRARY=ROOT/'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS/01-HEX-HEAD'
LANGUAGES=('cs','en','de','fr','ru')
FIELDS=('name','standard','stock','material','quantity','mass')
spec=importlib.util.spec_from_file_location('installer',ROOT/'tools/install-fastener-library.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def read_native(path):
    parser=configparser.RawConfigParser();parser.read(path,encoding='utf-8')
    return parser


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('manifest','old-manifest','models','thumbnails','references','verification'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    variants=json.loads(args.manifest.read_text(encoding='utf-8'))['variants']
    old=json.loads(args.old_manifest.read_text(encoding='utf-8'))['variants']
    records={v['filename']:v for v in json.loads((args.models/'consolidation.json').read_text())}
    verification={v['filename']:v for v in json.loads(args.verification.read_text())}
    illustrations=json.loads((args.references/'illustrations.json').read_text(encoding='utf-8'))
    assert len(variants)==len(records)==200 and len(illustrations)==10
    removals=set();copies=[];changes={};identities=set()
    for previous in old:
        folder=LIBRARY/previous['family'];name=previous['filename']
        removals.update((folder/name,folder/'0000-index/thumbnails'/(name+'.png')))
    for row in illustrations:
        variant=next(v for v in variants if v['standard']==row['standard'] and v['diameter_mm']==8)
        family=variant['family'];folder=LIBRARY/family/'0000-index'
        previous=next(v for v in old if v['standard']==row['standard'] and v['diameter_mm']==8 and v['product_grade']==variant['product_grade'])
        for suffix in ('.prtz','.drwz','.png'):
            removals.add(folder/(Path(previous['filename']).stem+'-REFERENCE'+suffix))
        stage=args.references/row['standard'].replace('ČSN','CSN').replace(' ','')
        part=stage/row['filename'];drawing=stage/row['drawing']
        native=read_native(part);identity=native['Document']['document_id']
        assert identity not in identities;identities.add(identity)
        document=json.loads(read_native(drawing)['Document']['param.cpp_drawing'])
        assert document['source_path']==part.name
        assert all(v['source_path']==part.name for s in document['sheets'] for v in s['views'])
        assert row['languages']==list(LANGUAGES)
        for source in [part,drawing,part.with_suffix('.png'),*[stage/(part.stem+'_'+language+'.png') for language in LANGUAGES]]:
            assert source.is_file(),source
            copies.append((source,folder/source.name))
    from PIL import Image,ImageChops
    for variant in variants:
        name=variant['filename'];record=records[name];source=args.models/name
        assert digest(source)==record['sha256']
        case=verification[record['previous_filename']]
        assert case['source_sha256']==record['source_sha256']
        assert case['exact_geometry_after_each_edit'] and case['undo_redo'] and case['save_reopen']
        native=read_native(source);identity=native['Document']['document_id']
        assert identity==record['document'] and identity not in identities;identities.add(identity)
        values=json.loads(native['UserParameterValues']['data'])
        assert 'product_grade' not in values and set(values['name'])==set(LANGUAGES)
        image=args.thumbnails/(name+'.png')
        with Image.open(image) as bitmap:
            assert bitmap.size==(320,240)
            bounds=ImageChops.difference(bitmap.convert('RGB'),Image.new('RGB',bitmap.size,'white')).getbbox()
            assert bounds and min(bounds[0],bounds[1],320-bounds[2],240-bounds[3])>=8,(image,bounds)
        folder=LIBRARY/variant['family'];index=folder/'0000-index'
        copies.extend(((source,folder/name),(image,index/'thumbnails'/image.name)))
        ini=changes.setdefault(index/'metadata.ini',{'Directory':{'Parameters':', '.join(FIELDS),'AutoIndex':'true'},'Parameters':{},'Parts':{},'Files':{}})
        for key in FIELDS:
            for language in LANGUAGES:
                ini['Parameters'][key+'\\Label\\'+language]=native['UserParameterLabels'][key+'\\'+language]
            for language,value in values[key].items():
                ini['Parts'][source.stem+'\\'+key+('\\'+language if language else '')]=value
        ini['Files'].update({source.stem+'\\Path':name,source.stem+'\\Provider':'zima-cad',
            source.stem+'\\Thumbnail':'0000-index/thumbnails/'+image.name})
    # The complete preflight precedes all mutations. Only unchanged Git-owned
    # files are retired; no wildcard delete and no recursive directory cleanup.
    for path in removals:
        assert path.resolve().is_relative_to(LIBRARY.resolve())
        committed=subprocess.check_output(['git','show','HEAD:'+path.relative_to(ROOT).as_posix()],cwd=ROOT)
        assert path.is_file() and path.read_bytes()==committed,'Modified factory file: '+str(path)
    for source,target in copies:
        assert target.resolve().is_relative_to(LIBRARY.resolve())
        assert not target.exists() or target in removals,'Destination exists: '+str(target)
    for path in removals:path.unlink()
    for source,target in copies:
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for path,values in changes.items():
        family=path.parent.parent.name
        stems={Path(v['filename']).stem for v in old if v['family']==family}
        section='';kept=[]
        for line in path.read_text(encoding='utf-8-sig').splitlines():
            if line.startswith('[') and line.endswith(']'):section=line[1:-1]
            if '=' in line:
                key=line.split('=',1)[0]
                if section in ('Parts','Files') and key.split('\\',1)[0] in stems:continue
                if section=='Parameters' and key.startswith('product_grade\\'):continue
            kept.append(line)
        path.write_text('\n'.join(kept)+'\n',encoding='utf-8')
        installer.update_ini(path,values)
        path.write_text(path.read_text(encoding='utf-8').replace('Parameters="'+', '.join(FIELDS)+'"','Parameters='+', '.join(FIELDS)),encoding='utf-8')
    print(f'Promoted {len(variants)} Parts, horizontal thumbnails and {len(illustrations)} localized native illustrations')


if __name__=='__main__':main()
