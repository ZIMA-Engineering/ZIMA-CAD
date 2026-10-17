"""Install independently verified factory fasteners and native reference artwork."""
import argparse
import configparser
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import shutil
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
LIBRARY=ROOT/'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS/01-HEX-HEAD'
LANGUAGES=('cs','en','de','fr','ru')


def qvalue(value):
    value=str(value).replace('\\','\\\\').replace('"','\\"').replace('\n','\\n')
    return '"'+value+'"' if any(c in value for c in ',;"') else value


def update_ini(path,changes):
    lines=path.read_text(encoding='utf-8-sig').splitlines()
    section='';output=[];pending={s:dict(v) for s,v in changes.items()}
    def finish():
        output.extend(k+'='+qvalue(v) for k,v in pending.pop(section,{}).items())
    for line in lines:
        if line.startswith('[') and line.endswith(']'):
            finish();section=line[1:-1]
        elif '=' in line and section in pending:
            key=line.split('=',1)[0]
            if key in pending[section]:line=key+'='+qvalue(pending[section].pop(key))
        output.append(line)
    finish()
    for section,items in pending.items():
        output.append('['+section+']')
        output.extend(k+'='+qvalue(v) for k,v in items.items())
    path.write_text('\n'.join(output)+'\n',encoding='utf-8')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--stage',type=Path,action='append',required=True)
    parser.add_argument('--verification',type=Path,required=True)
    parser.add_argument('--thumbnails',type=Path,required=True)
    parser.add_argument('--references',type=Path,required=True)
    args=parser.parse_args()
    manifest=json.loads(args.manifest.read_text(encoding='utf-8'))
    cases=json.loads(args.verification.read_text(encoding='utf-8'))
    verified={v['filename']:v for v in cases}
    assert len(verified)==len(cases)==len(manifest['variants'])==200
    prepared=[];identities=set()
    for variant in manifest['variants']:
        name=variant['filename']
        paths=[p.resolve()/name for p in args.stage if (p/name).is_file()]
        assert len(paths)==1,name
        source=paths[0];digest=hashlib.sha256(source.read_bytes()).hexdigest()
        case=verified[name]
        assert case['source_sha256']==digest and case['exact_geometry_after_each_edit'] and case['undo_redo'] and case['save_reopen'],name
        image=args.thumbnails/(name+'.png')
        with Image.open(image) as bitmap:
            assert bitmap.size==(320,240),image
            bitmap.verify()
        native=configparser.RawConfigParser();native.read(source,encoding='utf-8')
        identity=native['Document']['document_id']
        assert identity not in identities,'Duplicate native Part identity'
        identities.add(identity)
        values=json.loads(native['UserParameterValues']['data'])
        assert set(values['name'])==set(LANGUAGES) and float(values['mass'][''])>0,name
        labels={key:{language:native['UserParameterLabels'][key+'\\'+language] for language in LANGUAGES}
            for key in ('name','standard','stock','material','quantity','mass')}
        prepared.append((variant,source,image,values,labels))
    references=[]
    for standard in sorted({v['standard'] for v in manifest['variants']}):
        folder=standard.replace('ČSN','CSN').replace(' ','')
        representative=next(v for v in manifest['variants'] if v['standard']==standard and
            v['diameter_mm']==8 and (not standard.startswith('ČSN') or not v['fine_pitch']))
        source=args.references/folder/(Path(representative['filename']).stem+'-REFERENCE')
        extensions=('.prtz','.drwz','.png',*('_'+language+'.png' for language in LANGUAGES))
        for extension in extensions:
            assert Path(str(source)+extension).is_file(),source
        native_part=configparser.RawConfigParser();native_part.read(Path(str(source)+'.prtz'),encoding='utf-8')
        identity=native_part['Document']['document_id']
        assert identity not in identities,'Reference Part must have an independent native identity'
        identities.add(identity)
        native=configparser.RawConfigParser();native.read(Path(str(source)+'.drwz'),encoding='utf-8')
        drawing=json.loads(native['Document']['param.cpp_drawing'])
        assert drawing['source_path']==source.name+'.prtz'
        assert all(v['source_path']==drawing['source_path'] for s in drawing['sheets'] for v in s['views'])
        family=next(v['family'] for v in manifest['variants'] if v['standard']==standard)
        for extension in extensions:
            destination=LIBRARY/family/'0000-index'/(source.name+extension)
            if destination.exists():raise RuntimeError(f'Reference artwork already exists: {destination}')
        references.append((family,source))
    # Nothing is installed until every candidate and every required image passes.
    changes={}
    for variant,source,image,values,labels in prepared:
        family=variant['family'];folder=LIBRARY/family;index=folder/'0000-index'
        thumbnails=index/'thumbnails';thumbnails.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,folder/source.name)
        shutil.copy2(image,thumbnails/image.name)
        ini=changes.setdefault(index/'metadata.ini',{'Directory':{'Parameters':'name, standard, stock, material, quantity, mass','AutoIndex':'true'},'Parameters':{},'Parts':{},'Files':{}})
        # QSettings list syntax is authored directly after serializing scalar values.
        stem=source.stem
        for key,translations in labels.items():
            for language,label in translations.items():ini['Parameters'][key+'\\Label\\'+language]=label
        for key in ('name','standard','stock','material','quantity','mass'):
            for language,value in values[key].items():
                ini['Parts'][stem+'\\'+key+('\\'+language if language else '')]=value
        for key,labels in {
            'mass':dict(cs='Hmotnost [kg]',en='Mass [kg]',de='Masse [kg]',fr='Masse [kg]',ru='Масса [kg]')}.items():
            for language,label in labels.items():ini['Parameters'][key+'\\Label\\'+language]=label
        modified=datetime.fromtimestamp(source.stat().st_mtime,timezone.utc).isoformat(timespec='milliseconds').replace('+00:00','Z')
        ini['Files'].update({stem+'\\Path':source.name,stem+'\\Provider':'zima-cad',stem+'\\ModifiedUtc':modified,
            stem+'\\Thumbnail':'0000-index/thumbnails/'+image.name})
    for family,source in references:
        index=LIBRARY/family/'0000-index'
        for extension in ('.prtz','.drwz','.png',*('_'+language+'.png' for language in LANGUAGES)):
            original=Path(str(source)+extension)
            destination=index/original.name
            if destination.exists():raise RuntimeError(f'Reference artwork already exists: {destination}')
            shutil.copy2(original,destination)
    for path,values in changes.items():
        update_ini(path,values)
        # Parameters is a QStringList, rather than a quoted scalar containing commas.
        text=path.read_text(encoding='utf-8').replace('Parameters="name, standard, stock, material, quantity, mass"',
            'Parameters=name, standard, stock, material, quantity, mass')
        path.write_text(text,encoding='utf-8')
    print(f'Installed {len(prepared)} native fasteners, thumbnails and {len(references)} native reference Drawings')


if __name__=='__main__':main()
