"""Prepare the reviewed primary CSN closed-nut page without altering DIN data."""
import argparse
import copy
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LANGUAGES=('cs','en','de','fr','ru')


def prepare(data,models,art):
    rows=json.loads((models/'manifest.json').read_text(encoding='utf-8'))['variants']
    assert len(rows)==11 and all(v['closed_profile']=='cone_flat' for v in rows)
    parent=next(p for p in data['pages'] if p['path']=='05-NUTS/04-CAP')
    if parent.get('source_table'):
        assert not any(p['path']==parent['path'] for p in data.get('retired_pages',[]))
        data.setdefault('retired_pages',[]).append(copy.deepcopy(parent))
        replacement={k:parent[k] for k in ('path','title','id')}
        replacement.update(headers=['symbol','meaning'],rows=[],notes=[],sources=[],sections=[])
        parent.clear();parent.update(replacement)
    data['labels']['csn_closed_nuts']=['Uzavřené matice ČSN','CSN closed nuts','Geschlossene Muttern ČSN','Écrous borgnes ČSN','Закрытые гайки ČSN']
    data['labels']['csn_closed_heading']=['ČSN 02 1431:1970']*5
    data['labels']['csn_closed_geometry']=[
        'Zakončení pod 45° s rovnou kruhovou ploškou Øk; ústí závitu má vrcholový úhel 120°. k je průměr plošky. Otvor má rovné dno v hloubce p. Dosazení je na XY v Z = 0, osa závitu prochází počátkem.',
        'Closed end at 45° with a flat circular face Øk; the thread mouth has a 120° included angle. k is the flat diameter. The bore has a flat bottom at depth p. The seating plane is XY at Z = 0 and the thread axis passes through the Origin.',
        'Abschluss unter 45° mit ebener Kreisfläche Øk; die Gewindemündung hat einen Öffnungswinkel von 120°. k ist der Flächendurchmesser. Die Bohrung endet eben in Tiefe p. Die Auflage liegt in XY bei Z = 0; die Gewindeachse geht durch den Ursprung.',
        'Extrémité à 45° avec une face circulaire plane Øk ; l’entrée du filetage a un angle inclus de 120°. k est le diamètre du méplat. Le trou a un fond plat à la profondeur p. L’appui est sur XY à Z = 0 et l’axe du filetage passe par l’origine.',
        'Закрытый конец под 45° с плоской круглой площадкой Øk; угол входа резьбы равен 120°. k — диаметр площадки. Отверстие имеет плоское дно на глубине p. Опорная плоскость — XY при Z = 0; ось резьбы проходит через начало координат.']
    data['labels']['csn_closed_scope']=[
        'Rozměry z původní tabulky 1 ČSN z roku 1970, pouze doložená řada s hrubým stoupáním M6–M30. b je tabulkové minimum použité v modelu, e_min mezní rozměr přes vrcholy; skutečné (e) modelu se počítá z jmenovitého s. Schematický závit, ocel 7 850 kg/m³, hmotnost počítaná z objemu. Nejde o úplný výrobní předpis ani rozměrovou náhradu DIN 1587. Starší rozporná tabulka a kulové studie nejsou podkladem této řady.',
        'Dimensions from original 1970 CSN Table 1, limited to the evidenced coarse-pitch M6–M30 series. b is the tabulated minimum used by the model; e_min is the limiting width across corners, while actual model (e) is calculated from nominal s. Schematic thread, steel density 7,850 kg/m³, mass calculated from volume. This is not a complete manufacturing specification or a dimensionally identical DIN 1587 replacement. The contradictory older table and spherical studies are not the basis of this family.',
        'Maße aus der ursprünglichen ČSN-Tabelle 1 von 1970, beschränkt auf die belegte Regelgewindereihe M6–M30. b ist das im Modell verwendete tabellierte Minimum; e_min ist das Eckgrenzmaß, das tatsächliche Modellmaß (e) wird aus Nenn-s berechnet. Schematisches Gewinde, Stahldichte 7.850 kg/m³, Masse aus dem Volumen berechnet. Keine vollständige Fertigungsvorschrift und kein maßgleicher Ersatz für DIN 1587. Die widersprüchliche ältere Tabelle und Kugelstudien sind keine Grundlage dieser Reihe.',
        'Dimensions de la table 1 ČSN originale de 1970, limitées à la série attestée à pas gros M6–M30. b est le minimum tabulé utilisé dans le modèle ; e_min est la limite sur angles, tandis que la cote réelle (e) est calculée à partir de s nominal. Filetage schématique, acier de masse volumique 7 850 kg/m³, masse calculée par le volume. Ce n’est ni une spécification complète de fabrication ni un remplacement dimensionnellement identique de DIN 1587. L’ancienne table contradictoire et les études sphériques ne fondent pas cette série.',
        'Размеры из исходной таблицы 1 ČSN 1970 года, только подтверждённый ряд с крупным шагом M6–M30. b — табличный минимум, используемый в модели; e_min — предельный размер по вершинам, фактический размер модели (e) вычисляется по номинальному s. Резьба условная, плотность стали 7 850 кг/м³, масса вычисляется по объёму. Это не полное производственное предписание и не размерно идентичная замена DIN 1587. Противоречивая старая таблица и сферические эскизы не являются основой этого ряда.']
    fields=[('P','pitch_mm'),('s','across_flats_mm'),('e_min','minimum_across_corners_mm'),('h','height_mm'),('k','flat_diameter_mm'),('b min','thread_length_mm'),('p','bore_depth_mm')]
    headers=['csn_closed_d']
    data['labels']['csn_closed_d']=['d']*5
    for i,(symbol,_) in enumerate(fields):
        key='csn_closed_column_'+str(i);headers.append(key);data['labels'][key]=[symbol+' [mm]']*5
    figure=json.loads((art/'illustrations.json').read_text(encoding='utf-8'));assert len(figure)==1
    stem=Path(figure[0]['part']).stem
    data['labels']['csn_closed_art']=['ČSN 02 1431 — '+v for v in ('výkres nativního modelu','native model drawing','Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
    data['sources']['csn021431_1970']=dict(label='ČSN 02 1431:1970 · original preview, Table 1',url=rows[0]['source_url'])
    page=dict(path='05-NUTS/04-CAP/01-CSN021431',title='csn_closed_nuts',heading='csn_closed_heading',headers=headers,
        rows=[['M'+format(v['diameter_mm'],'g')+'×'+format(v['pitch_mm'],'g'),*[format(v[key],'.6g') for _,key in fields]] for v in rows],
        notes=['units','csn_closed_geometry','csn_closed_scope'],sources=['csn021431_1970'],freeze_header=True,
        localized_illustrations=True,figures=[dict(path=stem+'.png',caption='csn_closed_art',localized_paths={lang:stem+'_'+lang+'.png' for lang in LANGUAGES})])
    assert not any(p['path']==page['path'] for p in data['pages'])
    data['pages'].append(page)
    return data


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('models','art','output'):parser.add_argument('--'+key,type=Path,required=True)
    args=parser.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    args.output.write_text(json.dumps(prepare(data,args.models,args.art),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
