"""Create framed native washer Drawings with reviewed selected dimensions."""
import importlib.util
import json
from datetime import date
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('framed',ROOT/'tools/render-formed-nose-washer-library.py')
framed=importlib.util.module_from_spec(spec);spec.loader.exec_module(framed)
shaped=framed.shaped
original_notes=shaped.notes


def notes(variant,language):
    result=original_notes(variant,language)
    if 'section_selection' in variant:
        label=('Rozměry zvolené v tolerancích normy','Dimensions selected within the standard tolerances',
            'Maße innerhalb der Normtoleranzen gewählt','Dimensions choisies dans les tolérances de la norme',
            'Размеры выбраны в пределах допусков стандарта')[shaped.LANGUAGES.index(language)]
        dr=variant['sphere_radius_mm']-variant['source_nominal_sphere_radius_mm']
        dh=variant['height_mm']-variant['source_nominal_height_mm']
        result['texts'].append(dict(text=label,x_mm=190,y_mm=160,height_mm=2.1))
        result['texts'].append(dict(text=f'R = {variant["sphere_radius_mm"]:g} mm; h6 = {variant["height_mm"]:g} mm; ΔR = +{dr:g} mm; Δh = +{dh:g} mm',
            x_mm=190,y_mm=155,height_mm=2.1))
    return result


def main():
    shaped.notes=notes
    shaped.main(reference_stem=lambda v:'SW-D'+format(v['bore_mm'],'g'),sheet_scale=2)
    out=Path(sys.argv[sys.argv.index('--output')+1]).resolve()
    checker=shaped.pin_checker(out/'title-verification')
    for row in json.loads((out/'illustrations.json').read_text(encoding='utf-8')):
        folder=out/row['folder'];part=folder/row['part']
        native=shaped.art.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'title-source')
        try:
            native.run('open',path=str(part));parameters=native.run('document.parameters.get')['parameters']
            for parameter in parameters:
                if parameter['key']=='name':
                    parameter['values']=dict(zip(shaped.LANGUAGES,('Kulová podložka','Spherical washer',
                        'Kugelfederring','Rondelle sphérique','Сферическая шайба')))
                if parameter['key'] in ('drawn_by','date'):
                    parameter['values']={'':'ZIMA-Engineering' if parameter['key']=='drawn_by' else date.today().isoformat()}
            native.run('document.parameters.set',parameters=parameters);native.run('save');native.run('close')
        finally:native.close()
        stem=Path(row['drawing']).stem
        for language in ('',*shaped.LANGUAGES):
            drawing=folder/(stem+('_'+language if language else '')+'.drwz')
            subprocess.run([str(checker),'--reference-title',str(drawing),language or 'en'],check=True)
            subprocess.run([str(checker),'--adopt-reference-source',str(drawing),str(part),str(drawing)],check=True)
    framed.verify_titles(out)


if __name__=='__main__':main()
