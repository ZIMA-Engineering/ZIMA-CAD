"""Add the reviewed DIN 1587 CAD family without replacing historical CSN data."""
import argparse
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models',type=Path,required=True)
    parser.add_argument('--art',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    variants=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']
    assert len(variants)==26 and {v['standard'] for v in variants}=={'DIN 1587'}
    path='05-NUTS/04-CAP/02-DIN1587'
    assert not any(p['path']==path for p in data['pages']),'DIN page already prepared'
    title='directory_din_cap_nuts'
    data['labels'][title]=['Matice s půlkulatým vrchlíkem','Hemispherical cap nuts',
        'Muttern mit halbkugelförmiger Kappe','Écrous à calotte hémisphérique','Гайки с полусферическим колпачком']
    heading='din1587_nominal_cad';data['labels'][heading]=['DIN 1587']*5
    note='din1587_cad_dimensions'
    data['labels'][note]=[
        'Samostatná rozměrová řada DIN 1587; nejde o rozměrově shodnou náhradu historické ČSN. s je jmenovitý rozměr přes plochy, h a m tabulkové maximální výšky a R přibližný poloměr vrchlíku. Hodnoty označené CAD popisují model: (e) je vypočtený rozměr přes vrcholy, t zvolený střed tabulkového intervalu délky závitu a p = min(h − w_min, t + P). Skutečné dno v ose je w = h − p; w_min je tabulkové minimum. Závit a výběh 0,5P jsou schematické. Nejde o úplný výrobní předpis ani ověření všech požadavků vydání 2021.',
        'Separate DIN 1587 dimensional family; not a dimensionally identical replacement for historical CSN. s is the nominal width across flats, h and m the tabulated maximum heights, and R the approximate cap radius. Values marked CAD describe the model: (e) is the calculated width across corners, t the selected midpoint of the tabulated thread-depth interval, and p = min(h − w_min, t + P). Actual axial end wall is w = h − p; w_min is the tabulated minimum. Thread and 0.5P runout are schematic. This is not a complete manufacturing specification or verification of all 2021 edition requirements.',
        'Eigene Maßreihe nach DIN 1587; kein maßgleicher Ersatz für historische ČSN. s ist die Nennschlüsselweite, h und m sind tabellierte Höchstwerte, R der ungefähre Kappenradius. CAD-Werte beschreiben das Modell: (e) ist das berechnete Eckmaß, t der gewählte Mittelwert des tabellierten Gewindetiefenbereichs und p = min(h − w_min, t + P). Die tatsächliche axiale Bodenstärke ist w = h − p; w_min ist das tabellierte Minimum. Gewinde und Auslauf 0,5P sind schematisch. Dies ist weder eine vollständige Fertigungsvorschrift noch eine Prüfung aller Anforderungen der Ausgabe 2021.',
        'Série dimensionnelle DIN 1587 distincte ; elle ne remplace pas à dimensions identiques la série ČSN historique. s est la largeur nominale sur plats, h et m les hauteurs maximales tabulées et R le rayon approximatif de la calotte. Les valeurs CAO décrivent le modèle : (e) est la largeur calculée sur angles, t le milieu choisi de l’intervalle tabulé de profondeur filetée et p = min(h − w_min, t + P). Le fond axial réel vaut w = h − p ; w_min est le minimum tabulé. Filetage et sortie de 0,5P sont schématiques. Il ne s’agit ni d’une spécification complète de fabrication ni d’une vérification de toutes les exigences de l’édition 2021.',
        'Отдельный размерный ряд DIN 1587; он не является размерно идентичной заменой исторического ряда ČSN. s — номинальный размер под ключ, h и m — табличные максимальные высоты, R — приблизительный радиус колпачка. Значения CAD описывают модель: (e) — расчётный размер по вершинам, t — выбранная середина табличного интервала глубины резьбы, p = min(h − w_min, t + P). Фактическая осевая толщина дна w = h − p; w_min — табличный минимум. Резьба и сбег 0,5P показаны условно. Это не полное технологическое предписание и не проверка всех требований редакции 2021.']
    headers=['d','P','s','h max','m max','R ≈','(e) CAD','t CAD','p CAD','w_min','w CAD']
    keys=['din1587_header_'+str(i) for i in range(len(headers))]
    for key,label in zip(keys,headers):data['labels'][key]=[label]*5
    number=lambda value:format(value,'.6g')
    rows=[]
    for v in variants:
        rows.append(['M'+number(v['diameter_mm'])+'×'+number(v['pitch_mm']),
            *[number(v[k]) for k in ('pitch_mm','across_flats_mm','height_mm','hex_height_mm','dome_radius_mm')],
            number(2*v['across_flats_mm']/math.sqrt(3)),
            *[number(v[k]) for k in ('thread_length_mm','bore_depth_mm','minimum_pole_wall_mm','pole_wall_mm')]])
    source='din1587_aspen_dimensions'
    data['sources'][source]=dict(label='Aspen Fasteners — DIN 1587',url=variants[0]['source_url'])
    review=json.loads((ROOT/'tools/data/din-cap-nut-reviewed-dimensions.json').read_text(encoding='utf-8'))
    source_keys=[source]
    for i,url in enumerate(review['sources'][1:]):
        key='din1587_support_'+str(i);source_keys.append(key)
        data['sources'][key]=dict(label=('Böllhoff — DIN 1587','DIN 1587:1987','DIN 1587:2021 preview')[i],url=url)
    illustrations=json.loads((args.art/'illustrations.json').read_text(encoding='utf-8'));assert len(illustrations)==1
    stem=Path(illustrations[0]['part']).stem;caption='din1587_native_drawing'
    data['labels'][caption]=['DIN 1587 — '+s for s in ('výkres nativního modelu','native model drawing',
        'Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
    data['pages'].append(dict(path=path,title=title,heading=heading,headers=keys,rows=rows,
        notes=['units',note],sources=source_keys,freeze_header=True,localized_illustrations=True,
        figures=[dict(path=stem+'.png',caption=caption,
            localized_paths={lang:stem+'_'+lang+'.png' for lang in data['languages']})]))
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
