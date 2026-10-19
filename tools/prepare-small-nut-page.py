"""Present both verified native small-nut executions with their real symbols."""
import argparse
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--hex-art',type=Path,required=True);p.add_argument('--castle-art',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    page=next(p for p in data['pages'] if p['path']=='05-NUTS/03-SMALL')
    figures=[]
    for folder in (args.hex_art,args.castle_art):
        rows=json.loads((folder/'illustrations.json').read_text(encoding='utf-8'));assert len(rows)==1
        row=rows[0];stem=Path(row['part']).stem;caption='native_small_'+row['standard'].replace('ČSN','CSN').replace(' ','')
        data['labels'][caption]=[row['standard']+' — '+s for s in ('výkres nativního modelu','native model drawing',
            'Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
        figures.append(dict(path=stem+'.png',caption=caption,
            localized_paths={lang:stem+'_'+lang+'.png' for lang in data['languages']}))
    note='small_nut_native_executions'
    data['labels'][note]=[
        'm1 označuje výšku malé šestihranné matice, m2 celkovou výšku malé korunové matice a h výšku dna drážky. Dosedací plocha modelů je na XY při Z = 0, osa závitu v počátku. Od M12 je v modelu zvolen průměr koruny s; c je osové odlehčení podle tabulky. Rozměry zachovávají historické podklady ČSN.',
        'm1 is the small hexagon-nut height, m2 the overall small castle-nut height, and h the slot-floor height. Models seat on XY at Z = 0 with the thread axis at the Origin. From M12, the selected CAD crown diameter equals s; c is the tabulated axial relief. Dimensions retain the historical CSN data.',
        'm1 ist die Höhe der kleinen Sechskantmutter, m2 die Gesamthöhe der kleinen Kronenmutter und h die Höhe des Schlitzgrundes. Die Modelle liegen auf XY bei Z = 0, die Gewindeachse im Ursprung. Ab M12 ist als CAD-Kronendurchmesser s gewählt; c ist der tabellierte axiale Freistich. Die Maße entsprechen den historischen ČSN-Unterlagen.',
        'm1 est la hauteur du petit écrou hexagonal, m2 la hauteur totale du petit écrou à créneaux et h celle du fond des fentes. Les modèles reposent sur XY à Z = 0 avec l’axe du filetage à l’origine. À partir de M12, le diamètre de couronne choisi en CAO vaut s ; c est le dégagement axial du tableau. Les dimensions conservent les données ČSN historiques.',
        'm1 — высота малой шестигранной гайки, m2 — общая высота малой корончатой гайки, h — высота дна паза. Опорная плоскость моделей — XY при Z = 0, ось резьбы проходит через начало координат. Начиная с M12 выбранный диаметр короны в модели равен s; c — осевая разгрузка из таблицы. Размеры соответствуют историческим данным ČSN.']
    page.update(figures=figures,localized_illustrations=True,figures_side_by_side=True)
    if note not in page['notes']:page['notes'].append(note)
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
