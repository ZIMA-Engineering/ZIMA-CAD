"""Author small castle nuts using the archived dimensions and original figure.

For M12 and larger the table specifies axial crown relief c, but no crown
diameter. The CAD execution uses the inscribed diameter s; do not present this
selected diameter as an additional tabulated normative dimension.
"""
import argparse
import importlib.util
import json
import hashlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
def module(name,filename):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/filename)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value
small=module('small','generate-small-hex-nut-library.py')
castle=module('castle','generate-castle-nut-library.py')


def variants():
    table=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))['05-NUTS/03-SMALL']['tables'][0]
    number=lambda x:float(x.strip('() ').replace(',','.'))
    result=[]
    for v in small.variants():
        column=next(i for i,text in enumerate(table[0][1:],1) if number(text.strip('M()'))==v['diameter_mm'])
        crown=v['diameter_mm']>=12
        v.update(filename=v['filename'].replace('SHN-CSN021402','SCN-CSN021413'),standard='ČSN 02 1413',
            height_mm=number(table[6][column]),height_symbol='m2',slot_floor_symbol='h',slot_width_mm=number(table[8][column]),slot_floor_mm=number(table[7][column]),
            crown_diameter_mm=v['across_flats_mm'] if crown else None,
            crown_relief_mm=number(table[9][column]) if crown else 0,crown_diameter_from_flats=crown,
            names=dict(zip(small.nuts.caps.LANGUAGES,('Malá korunová matice','Small castle nut','Kleine Kronenmutter',
                'Petit écrou à créneaux','Малая корончатая гайка'))),
            review_status='historical dimensions; selected inscribed crown diameter equals s; round slot roots R=n/2')
        result.append(v)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--filename');args=p.parse_args()
    stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);completed=[]
    for v in variants():
        if args.filename and v['filename']!=args.filename:continue
        target=stage/v['filename'];assert not target.exists(),target;evidence=stage/'evidence'/target.stem
        native=castle.nuts.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:
            ids=castle.nuts.author(native,v,target);castle.crown(native,v,target,ids);ids=castle.slots(native,v,target,ids)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        v['independent_volume_mm3']=castle.sections.volume(v);v['generated_sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
        completed.append(v);(stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)


if __name__=='__main__':main()
