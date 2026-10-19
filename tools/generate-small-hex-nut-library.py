"""Author the historical small-hexagon nut execution from archived dimensions.

The companion small castle execution is a separate family. Do not substitute
ordinary ISO hexagon nuts or infer an ISO equivalent from similar appearance.
"""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('nuts',ROOT/'tools/generate-hex-nut-library.py')
nuts=importlib.util.module_from_spec(spec);spec.loader.exec_module(nuts)


def variants():
    table=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))['05-NUTS/03-SMALL']['tables'][0]
    number=lambda x:float(x.strip('() ').replace(',','.'))
    result={}
    for header in table[:2]:
        for column,value in enumerate(header[1:],1):
            parts=value.strip('()').replace('×','x')[1:].split('x')
            diameter=number(parts[0]);pitch=number(parts[1]) if len(parts)>1 else None
            result[(diameter,pitch)]=dict(filename=f'SHN-CSN021402-M{diameter:04.1f}'+(f'x{pitch:04.2f}' if pitch else '')+'.prtz',
                standard='ČSN 02 1402',family='03-SMALL',diameter_mm=diameter,pitch_mm=pitch,
                across_flats_mm=number(table[2][column]),height_mm=number(table[4][column]),height_symbol='m1',chamfer_degrees=15,
                names=dict(zip(nuts.caps.LANGUAGES,('Malá šestihranná matice','Small hexagon nut','Kleine Sechskantmutter',
                    'Petit écrou hexagonal','Малая шестигранная гайка'))),
                density_kg_mm3=7.85e-6,material='Steel',fine_pitch=pitch is not None,
                review_status='historical dimensions; 15-degree outer bevel from the original ZIMA-Parts figure')
    return list(result.values())


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--filename');args=p.parse_args()
    stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);completed=[]
    for v in variants():
        if args.filename and v['filename']!=args.filename:continue
        target=stage/v['filename'];assert not target.exists(),target
        evidence=stage/'evidence'/target.stem
        native=nuts.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=nuts.author(native,v,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        completed.append(v);(stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)


if __name__=='__main__':main()
