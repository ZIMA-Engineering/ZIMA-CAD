"""Attach checked native nut art and reuse the established size-row presentation."""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tables',ROOT/'tools/generate-reference-tables.py')
tables=importlib.util.module_from_spec(spec);spec.loader.exec_module(tables)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--art',type=Path,required=True)
    p.add_argument('--models',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    rows=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']
    figures=json.loads((args.art/'illustrations.json').read_text(encoding='utf-8'))
    data['labels']['nut_origin_seating_note']=[
        'Model má dosedací plochu v rovině XY na Z = 0 a osu závitu v počátku. Výška matice směřuje do +Z.',
        'The model has its seating face on XY at Z = 0 and its thread axis through the Origin. Nut height extends in +Z.',
        'Die Auflagefläche des Modells liegt auf XY bei Z = 0, die Gewindeachse durch den Ursprung. Die Mutternhöhe erstreckt sich in +Z.',
        'La face d’appui du modèle est sur XY à Z = 0 et l’axe du filetage passe par l’origine. La hauteur s’étend vers +Z.',
        'Опорная поверхность модели находится в XY при Z = 0, ось резьбы проходит через начало координат. Высота направлена вдоль +Z.']
    primary={'ISO 4032':61668,'ISO 4033':61669,'ISO 4034':61670,'ISO 4035':61671,
        'ISO 4036':61672,'ISO 8673':61673,'ISO 8674':61674,'ISO 8675':61675}
    for row in figures:
        standard=row['standard'];family=next(v['family'] for v in rows if v['standard']==standard)
        page=next(p for p in data['pages'] if p['path']=='05-NUTS/'+family)
        stem=Path(row['part']).stem;caption='native_nut_'+standard.replace(' ','')
        data['labels'][caption]=[standard+' — '+s for s in ('výkres nativního modelu','native model drawing','Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
        page.update(figures=[dict(path=stem+'.png',caption=caption,localized_paths={lang:stem+'_'+lang+'.png' for lang in data['languages']})],
            localized_illustrations=True,thread_dimension_rows=True,freeze_header=True)
        page['display_row_count']=sum(len(b['rows']) for b in tables.screen_table_blocks(page))
        if 'nut_origin_seating_note' not in page['notes']:page['notes'].append('nut_origin_seating_note')
        key=standard.lower().replace(' ','')+'_2012_basis' if standard in primary else 'nut_archived_factory_dimensions'
        if standard in primary:
            data['sources'][key]=dict(label=standard+':2012',url=f'https://www.iso.org/standard/{primary[standard]}.html')
        else:
            data['sources'][key]=dict(label='ZIMA-Engineering · archived factory dimensions',
                url='https://github.com/ZIMA-Engineering/ZIMA-CAD/blob/main/tools/data/fastener-source-tables.json')
        if key not in page['sources']:page['sources'].insert(0,key)
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
