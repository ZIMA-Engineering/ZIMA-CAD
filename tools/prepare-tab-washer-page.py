"""Attach verified native tab-washer artwork without changing historical cells."""
import argparse
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--art',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    figures=json.loads((args.art/'illustrations.json').read_text(encoding='utf-8'))
    assert len(figures)==1 and figures[0]['standard']=='ČSN 02 1751'
    row=figures[0];stem=Path(row['part']).stem
    page=next(p for p in data['pages'] if p['path']=='06-WASHERS/05-LOCK')
    caption='native_tab_washer_reference'
    data['labels'][caption]=['ČSN 02 1751 — '+s for s in (
        'podložka s jazýčkem, výkres nativního modelu','tab washer, native model drawing',
        'Sicherungsblech mit Lappen, Zeichnung des nativen Modells',
        'rondelle-frein à languette, dessin du modèle natif','шайба с лапкой, чертёж нативной модели')]
    note='tab_washer_origin_note'
    data['labels'][note]=[
        'Model ČSN 02 1751 má střed otvoru v počátku, dosedací plochu na XY při Z = 0 a tloušťku ve směru +Z. Rozměr l se měří od osy otvoru ke konci jazýčku.',
        'The ČSN 02 1751 model has its bore center at the Origin, its seating face on XY at Z = 0 and thickness in +Z. Dimension l runs from the bore axis to the tab end.',
        'Beim Modell ČSN 02 1751 liegt die Bohrungsmitte im Ursprung, die Auflagefläche auf XY bei Z = 0 und die Dicke in +Z. Maß l reicht von der Bohrungsachse bis zum Lappenende.',
        'Le modèle ČSN 02 1751 place le centre de l’alésage à l’origine, la face d’appui sur XY à Z = 0 et l’épaisseur vers +Z. La cote l va de l’axe de l’alésage au bout de la languette.',
        'У модели ČSN 02 1751 центр отверстия находится в начале координат, опорная плоскость — XY при Z = 0, толщина направлена вдоль +Z. Размер l измеряется от оси отверстия до конца лапки.']
    page.update(figures=[dict(path=stem+'.png',caption=caption,
        localized_paths={lang:stem+'_'+lang+'.png' for lang in data['languages']})],localized_illustrations=True)
    if note not in page['notes']:page['notes'].append(note)
    key='tab_washer_historical_scan'
    data['sources'][key]=dict(label='ČSN 02 1751 / ČSN 02 1753 · historical tables, pp. 184–185',
        url='https://encyklopediapoznania.sk/data/eknihy/strojarenstvo/strojnicke_tabulky_z_roku_1965.pdf')
    page['sources']=[key]
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
