"""Author reviewed 3d coupling nuts using the established native hex-nut path."""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('nuts',ROOT/'tools/generate-hex-nut-library.py')
nuts=importlib.util.module_from_spec(spec);spec.loader.exec_module(nuts)


def variants():
    facts=json.loads((ROOT/'tools/data/coupling-nut-reviewed-dimensions.json').read_text(encoding='utf-8'))
    names=('Šestihranná spojovací matice, výška 3d','Hexagon coupling nut, height 3d',
        'Sechskant-Verbindungsmutter, Höhe 3d','Écrou hexagonal de raccordement, hauteur 3d',
        'Шестигранная соединительная гайка, высота 3d')
    result=[]
    for row in facts['rows']:
        v=dict(zip(facts['columns'],row));d=v['diameter_mm']
        assert v['height_mm']==3*d
        v.update(filename=f'CPN-DIN6334-M{d:04.1f}.prtz',standard='DIN 6334',family='11-COUPLING',
            pitch_mm=None,chamfer_degrees=30,names=dict(zip(nuts.caps.LANGUAGES,names)),
            density_kg_mm3=7.85e-6,material='Steel',fine_pitch=False,source_url=facts['source_url'],
            representation=facts['scope'],height_symbol='h',drawing_projection_distance_mm=55)
        result.append(v)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--filename');args=p.parse_args();stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True)
    completed=[]
    for v in variants():
        if args.filename and v['filename']!=args.filename:continue
        target=stage/v['filename'];assert not target.exists(),target
        evidence=stage/'evidence'/target.stem
        native=nuts.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=nuts.author(native,v,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        completed.append(v)
        (stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)


if __name__=='__main__':main()
