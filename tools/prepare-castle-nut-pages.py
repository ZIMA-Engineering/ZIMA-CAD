"""Merge archived castle-nut dimension groups into one row-per-size table."""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tables',ROOT/'tools/generate-reference-tables.py')
tables=importlib.util.module_from_spec(spec);spec.loader.exec_module(tables)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--models',type=Path);p.add_argument('--art',type=Path);args=p.parse_args()
    assert bool(args.models)==bool(args.art),'Models and native artwork must be supplied together'
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    data['labels']['castle_fine_thread']=['Jemný závit d','Fine thread d','Feingewinde d','Filetage fin d','Мелкая резьба d']
    data['labels']['castle_cotter_d']=['Závlačka D','Cotter pin D','Splint D','Goupille D','Шплинт D']
    data['labels']['castle_cotter_l']=['Závlačka l','Cotter pin l','Splint l','Goupille l','Шплинт l']
    for page in data['pages']:
        if not page['path'].startswith('05-NUTS/02-CASTLE/'):continue
        data['labels'][page['title']]=[
            'Nízké korunové matice','Thin castle nuts','Niedrige Kronenmuttern',
            'Écrous bas à créneaux','Низкие корончатые гайки'] if page['path'].endswith('/02-LOW') else [
            'Korunové matice','Castle nuts','Kronenmuttern','Écrous à créneaux','Корончатые гайки']
        for block in tables.table_blocks(page):
            original=data['curated_source_tables'][block['source_table']]['rows']
            labels={}
            for index,row in enumerate(block['rows']):
                row[0]=original[block['source_rows'][0]+index][0]
                label=None
                if row[0]=='d':label='castle_fine_thread'
                elif isinstance(row[0],dict):
                    key=row[0]['key']
                    if key.startswith('row_heading_') and key.endswith('_7'):label='castle_cotter_d'
                    elif key.startswith('row_heading_') and key.endswith('_8'):label='castle_cotter_l'
                if label:labels[json.dumps(row[0],sort_keys=True)]={'key':label}
            block['parameter_labels']=labels
        page.update(thread_dimension_rows=True,freeze_header=True)
        page['display_row_count']=sum(len(b['rows']) for b in tables.screen_table_blocks(page))
    if args.art:
        rows=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']
        figures=json.loads((args.art/'illustrations.json').read_text(encoding='utf-8'))
        assert len(rows)==53 and len(figures)==2
        note='castle_nut_native_model_note'
        data['labels'][note]=[
            'Nativní model má dosedací plochu na XY při Z = 0, osu závitu v počátku a šest drážek se zaobleným dnem R = n/2. h je celková výška, p výška dna drážky. Závit je v drážkách odstraněn. Rozměry zachovávají historickou tabulku ČSN.',
            'The native model seats on XY at Z = 0, with its thread axis at the Origin and six round-root slots, R = n/2. h is the overall height; p is the slot-floor height. The thread is removed inside the slots. Dimensions retain the historical CSN table.',
            'Das native Modell liegt auf XY bei Z = 0, mit Gewindeachse im Ursprung und sechs Schlitzen mit rundem Grund, R = n/2. h ist die Gesamthöhe, p die Höhe des Schlitzgrundes. In den Schlitzen ist das Gewinde entfernt. Die Maße entsprechen der historischen ČSN-Tabelle.',
            'Le modèle natif repose sur XY à Z = 0, avec l’axe du filetage à l’origine et six fentes à fond arrondi, R = n/2. h est la hauteur totale, p celle du fond des fentes. Le filetage est supprimé dans les fentes. Les dimensions conservent le tableau ČSN historique.',
            'Опорная плоскость нативной модели — XY при Z = 0, ось резьбы проходит через начало координат. Шесть пазов имеют скруглённое дно R = n/2. h — общая высота, p — высота дна паза. В пазах резьба удалена. Размеры соответствуют исторической таблице ČSN.']
        for row in figures:
            family=next(v['family'] for v in rows if v['standard']==row['standard'])
            page=next(p for p in data['pages'] if p['path']=='05-NUTS/'+family)
            stem=Path(row['part']).stem;caption='native_castle_'+row['standard'].replace('ČSN','CSN').replace(' ','')
            data['labels'][caption]=[row['standard']+' — '+s for s in (
                'výkres nativního modelu','native model drawing','Zeichnung des nativen Modells',
                'dessin du modèle natif','чертёж нативной модели')]
            page.update(figures=[dict(path=stem+'.png',caption=caption,
                localized_paths={lang:stem+'_'+lang+'.png' for lang in data['languages']})],localized_illustrations=True)
            if note not in page['notes']:page['notes'].append(note)
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
