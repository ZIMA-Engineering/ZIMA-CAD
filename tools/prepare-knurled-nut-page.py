"""Present both historical knurled-nut executions using native section drawings."""
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
    page=next(p for p in data['pages'] if p['path']=='05-NUTS/08-KNURLED')
    rows=json.loads((args.art/'illustrations.json').read_text(encoding='utf-8'))
    assert {r['standard'] for r in rows}=={'ČSN 02 1461','ČSN 02 1462'}
    figures=[]
    for row in rows:
        stem=Path(row['part']).stem
        caption='native_knurled_'+row['standard'].replace('ČSN','CSN').replace(' ','')
        data['labels'][caption]=[row['standard']+' — '+label for label in (
            'řez nativním modelem','native model section','Schnitt des nativen Modells',
            'coupe du modèle natif','разрез нативной модели')]
        figures.append(dict(path=stem+'.png',caption=caption,
            localized_paths={lang:stem+'_'+lang+'.png' for lang in data['languages']}))
    note='knurled_nut_native_executions'
    data['labels'][note]=[
        'h1 je výška vysoké matice, h2 výška nízké matice a k výška rýhované části. Dosedací plocha je na XY při Z = 0, osa závitu v počátku. Rozměry zachovávají historické tabulky ČSN. Model používá přímé rýhování s profilem 90°, zvolenou hloubkou t/4 a celočíselným počtem drážek; jde o geometrické znázornění, nikoli předpis výroby rýhování.',
        'h1 is the high-nut height, h2 the low-nut height, and k the knurled head height. The seating face is on XY at Z = 0 with the thread axis at the Origin. Dimensions retain the historical CSN tables. The model uses straight knurling with a 90° profile, a selected t/4 depth and an integer groove count; this is a geometric representation, not a knurl manufacturing specification.',
        'h1 ist die Höhe der hohen Mutter, h2 die Höhe der niedrigen Mutter und k die Höhe des Rändelkopfes. Die Auflagefläche liegt auf XY bei Z = 0, die Gewindeachse im Ursprung. Die Maße entsprechen den historischen ČSN-Tabellen. Das Modell verwendet eine gerade Rändelung mit 90°-Profil, gewählter Tiefe t/4 und ganzzahliger Rillenanzahl; dies ist eine geometrische Darstellung, keine Fertigungsvorschrift.',
        'h1 est la hauteur de l’écrou haut, h2 celle de l’écrou bas et k celle de la tête moletée. La face d’appui est sur XY à Z = 0, avec l’axe du filetage à l’origine. Les dimensions conservent les tableaux ČSN historiques. Le modèle utilise un moletage droit de profil 90°, de profondeur choisie t/4 et un nombre entier de rainures ; cette représentation géométrique ne constitue pas une spécification de fabrication.',
        'h1 — высота высокой гайки, h2 — высота низкой гайки, k — высота рифлёной части. Опорная плоскость — XY при Z = 0, ось резьбы проходит через начало координат. Размеры соответствуют историческим таблицам ČSN. В модели принята прямая накатка с профилем 90°, выбранной глубиной t/4 и целым числом канавок; это геометрическое изображение, а не технологическое предписание.']
    page.update(figures=figures,localized_illustrations=True,figures_side_by_side=True)
    if note not in page['notes']:page['notes'].append(note)
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
