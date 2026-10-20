"""Add reviewed pressed-wing rows without replacing the archived combined table."""
import argparse
import json
import uuid
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LANGUAGES=('cs','en','de','fr','ru')


def prepare(data,models,art):
    rows=json.loads((models/'manifest.json').read_text(encoding='utf-8'))['variants'];assert len(rows)==6
    figures=json.loads((art/'illustrations.json').read_text(encoding='utf-8'));assert len(figures)==1
    path='05-NUTS/09-WING/01-PRESSED';title='pressed_wing_title'
    data['labels'][title]=[v for v in rows[0]['names'].values()]
    assert list(rows[0]['names'])==list(LANGUAGES)
    data['labels']['pressed_wing_scope']=[
        'Šest nativních modelů M3–M10. Rozměry historické ČSN 02 1665 jsou porovnány s výběrem STN 02 1665. R1 je oblouk křídla, R2 oblouk náboje; archiv jej označuje R5. Zde h znamená celkovou výšku a p výšku náboje. Úhly neurčené tabulkou jsou zvoleny 70°; nejde o úplný výrobní předpis. Závit je schematický. Hmotnost se počítá z geometrie při hustotě oceli 7 850 kg/m³, nekopíruje se z orientační tabulky. Odlévané velikosti M12–M24 zůstávají samostatně rozpracované.',
        'Six native M3–M10 models. Historical CSN 02 1665 dimensions are compared with the STN 02 1665 excerpt. R1 is the wing arc and R2 the hub arc, called R5 in the archive. Here h is overall height and p hub height. Angles not specified by the table are selected as 70°; this is not a complete manufacturing specification. Threads are schematic. Mass is calculated from geometry at steel density 7,850 kg/m³, not copied from indicative weights. Cast M12–M24 sizes remain separate unfinished studies.',
        'Sechs native Modelle M3–M10. Historische Maße nach CSN 02 1665 sind mit dem Auszug STN 02 1665 verglichen. R1 bezeichnet den Flügelbogen, R2 den Nabenbogen, im Archiv R5. Hier ist h die Gesamthöhe und p die Nabenhöhe. Nicht angegebene Winkel sind mit 70° gewählt; keine vollständige Fertigungsvorschrift. Schematische Gewinde. Die Masse wird aus der Geometrie mit Stahldichte 7.850 kg/m³ berechnet, nicht aus Richtgewichten kopiert. Gegossene Größen M12–M24 bleiben separate unfertige Studien.',
        'Six modèles natifs M3–M10. Les dimensions historiques CSN 02 1665 sont comparées à l’extrait STN 02 1665. R1 est l’arc de l’oreille, R2 celui du moyeu, nommé R5 dans l’archive. Ici h est la hauteur totale et p celle du moyeu. Les angles non spécifiés sont choisis à 70° ; ce n’est pas une spécification complète de fabrication. Filetages schématiques. Masse calculée sur la géométrie avec une densité d’acier de 7 850 kg/m³, sans copier les masses indicatives. Les tailles moulées M12–M24 restent des études inachevées distinctes.',
        'Шесть нативных моделей M3–M10. Исторические размеры CSN 02 1665 сопоставлены с выдержкой STN 02 1665. R1 — дуга крыла, R2 — дуга ступицы, в архиве R5. Здесь h — общая высота, p — высота ступицы. Неуказанные углы приняты 70°; это не полная производственная спецификация. Резьба условная. Масса рассчитана по геометрии при плотности стали 7 850 кг/м³, а не скопирована из ориентировочной таблицы. Литые размеры M12–M24 остаются отдельными незавершёнными исследованиями.']
    fields=[('P','pitch_mm'),('d1','hub_top_diameter_mm'),('D','hub_base_diameter_mm'),('e','span_mm'),
        ('g1','tip_thickness_mm'),('g2','root_thickness_mm'),('h','height_mm'),('p','hub_height_mm'),
        ('R1','outside_radius_mm'),('R2','hub_radius_mm')]
    headers=['symbol_d']
    if 'symbol_d' not in data['labels']:data['labels']['symbol_d']=['d']*5
    for symbol,_ in fields:
        key='pressed_wing_'+symbol;headers.append(key);data['labels'][key]=[symbol+' [mm]']*5
    data['sources']['pressed_wing_stn']=dict(label='STN 02 1665 · historical excerpt, p. 16',
        url='https://cloud6.edupage.org/cloud/Vyber_noriem_PDF.pdf?z:LUPeiSrXT3T/KEbijWTB1cDo8rhSxS69cRvC%2Band2TKNbHFceQEhSDQH2eAjAm4J=')
    data['labels']['pressed_wing_art']=['ČSN 02 1665 · '+x for x in ('výkres nativního modelu','native model drawing',
        'Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
    stem=Path(figures[0]['part']).stem
    page=dict(path=path,title=title,heading=title,id=str(uuid.uuid5(uuid.NAMESPACE_URL,'https://zima-engineering.cz/zima-cad/library/'+path)),
        headers=headers,rows=[['M'+format(v['diameter_mm'],'g'),*[format(v[key],'.6g') for _,key in fields]] for v in rows],
        notes=['units','pressed_wing_scope'],sources=['pressed_wing_stn'],sections=[],freeze_header=True,
        omit_approximate_mass=True,display_row_count=6,localized_illustrations=True,
        figures=[dict(path=stem+'.png',caption='pressed_wing_art',localized_paths={lang:stem+'_'+lang+'.png' for lang in LANGUAGES})])
    existing=[i for i,p in enumerate(data['pages']) if p['path']==path]
    if existing:data['pages'][existing[0]]=page
    else:data['pages'].append(page)
    return data


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('models','art','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    a.output.write_text(json.dumps(prepare(data,a.models,a.art),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
