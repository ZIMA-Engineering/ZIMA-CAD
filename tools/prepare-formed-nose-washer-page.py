"""Add original-1962 nose-washer rows beside the preserved archived table."""
import argparse
import json
from pathlib import Path
import uuid

ROOT=Path(__file__).resolve().parents[1]
LANGUAGES=('cs','en','de','fr','ru')


def prepare(data,models,art):
    rows=json.loads((models/'manifest.json').read_text(encoding='utf-8'))['variants'];assert len(rows)==13
    figures=json.loads((art/'illustrations.json').read_text(encoding='utf-8'));assert len(figures)==1
    reviewed=json.loads((ROOT/'tools/data/formed-nose-washer-reviewed-dimensions.json').read_text(encoding='utf-8'))
    path='06-WASHERS/05-LOCK/02-FORMED-NOSE';title='formed_nose_title'
    data['labels'][title]=[rows[0]['names'][lang] for lang in LANGUAGES]
    data['labels']['formed_nose_scope']=[
        'Nativní ohnuté modely M4–M27 podle původního náhledu ČSN 02 1753 z roku 1962, nikoli ověření vydání 2003. Vnější D je u M4 14 mm a u M22 46 mm; společná archivní tabulka zůstává zachována. Tabule, skutečný ohyb 90° a navázaná rovná část nosu. Zvolený vnitřní poloměr r = t a K = 0,5. Hmotnost se počítá z geometrie při hustotě oceli 7 850 kg/m³. Rozměry a tolerance montážní díry nejsou nahrazeny zkušebním protikusem.',
        'Native formed M4–M27 models use the original 1962 CSN 02 1753 preview; the 2003 edition is not verified. Outside D is 14 mm for M4 and 46 mm for M22; the combined archived table remains preserved. Flat sheet, real 90° Bend and attached straight nose leg. Selected inside radius r = t and K = 0.5. Mass is calculated from geometry at steel density 7,850 kg/m³. Test fixtures do not replace the mounting-hole dimensions and tolerances.',
        'Native gebogene Modelle M4–M27 nach der ursprünglichen Vorschau CSN 02 1753 von 1962; Ausgabe 2003 nicht geprüft. Außen-D beträgt 14 mm für M4 und 46 mm für M22; die gemeinsame historische Tabelle bleibt erhalten. Blech, echte 90°-Biegung und angeschlossener gerader Nasenteil. Gewählter Innenradius r = t, K = 0,5. Masse aus der Geometrie mit Stahldichte 7.850 kg/m³. Prüfkörper ersetzen keine Maße und Toleranzen der Montagebohrung.',
        'Modèles natifs formés M4–M27 selon l’aperçu original CSN 02 1753 de 1962 ; édition 2003 non vérifiée. D extérieur vaut 14 mm pour M4 et 46 mm pour M22 ; le tableau historique commun reste conservé. Tôle, vrai pli à 90° et partie droite de l’ergot attachée. Rayon intérieur choisi r = t, K = 0,5. Masse calculée sur la géométrie avec une densité d’acier de 7 850 kg/m³. Les pièces d’essai ne remplacent pas les dimensions et tolérances du trou de montage.',
        'Нативные гнутые модели M4–M27 по исходному обзору CSN 02 1753 1962 года; издание 2003 не проверено. Наружный D равен 14 мм для M4 и 46 мм для M22; общая архивная таблица сохранена. Лист, настоящий гиб 90° и связанная прямая часть носика. Выбран внутренний радиус r = t, K = 0,5. Масса рассчитана по геометрии при плотности стали 7 850 кг/м³. Испытательные детали не заменяют размеры и допуски монтажного отверстия.']
    fields=[('d','bore_mm'),('D','outside_mm'),('f','nose_width_mm'),('l','root_distance_mm'),('h','height_mm'),('t','thickness_mm')]
    headers=['formed_nose_bolt'];data['labels']['formed_nose_bolt']=['M']*5
    for symbol,_ in fields:
        key='formed_nose_'+symbol;headers.append(key);data['labels'][key]=[symbol+' [mm]']*5
    data['sources']['formed_nose_1962']=dict(label='ČSN 02 1753 (1962)',url=reviewed['source_url'])
    data['labels']['formed_nose_art']=['ČSN 02 1753 (1962) · '+x for x in ('výkres nativního modelu','native model drawing',
        'Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
    stem=Path(figures[0]['part']).stem
    page=dict(path=path,title=title,heading=title,id=str(uuid.uuid5(uuid.NAMESPACE_URL,'https://zima-engineering.cz/zima-cad/library/'+path)),
        headers=headers,rows=[['M'+format(v['nominal_mm'],'g'),*[format(v[key],'.6g') for _,key in fields]] for v in rows],
        notes=['units','formed_nose_scope'],sources=['formed_nose_1962'],sections=[],freeze_header=True,
        omit_approximate_mass=True,display_row_count=13,localized_illustrations=True,
        figures=[dict(path=stem+'.png',caption='formed_nose_art',localized_paths={lang:stem+'_'+lang+'.png' for lang in LANGUAGES})])
    found=[i for i,p in enumerate(data['pages']) if p['path']==path]
    if found:data['pages'][found[0]]=page
    else:data['pages'].append(page)
    return data


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('models','art','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    a.output.write_text(json.dumps(prepare(data,a.models,a.art),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
