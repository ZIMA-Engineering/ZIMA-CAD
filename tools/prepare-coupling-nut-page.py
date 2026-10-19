"""Publish the reviewed coupling-nut dimensions and localized native art."""
import argparse
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def prepare(data,models,art):
    rows=json.loads((models/'manifest.json').read_text(encoding='utf-8'))['variants']
    assert len(rows)==14 and all(v['height_mm']==3*v['diameter_mm'] for v in rows)
    path='05-NUTS/11-COUPLING';assert not any(p['path']==path for p in data['pages'])
    data['labels']['coupling_title']=['Spojovací matice','Coupling nuts','Verbindungsmuttern','Écrous de raccordement','Соединительные гайки']
    data['labels']['coupling_heading']=['DIN 6334 · h = 3d']*5
    data['labels']['coupling_scope']=[
        'DIN 6334 je obchodní označení spojovacích matic, nikoliv potvrzení shody s vydanou normou DIN pro tento výrobek. Jmenovitá výška h = 3d, průchozí schematický metrický závit. Vnější sražení 30° a ústí závitu 90° jsou zvolené CAD detaily. Pozinkování a výrobní tolerance se nemodelují. Ocel 7 850 kg/m³; hmotnost se počítá z aktuálního objemu.',
        'DIN 6334 is a trade designation for coupling nuts, not certification to a published DIN standard for this product. Nominal height h = 3d, through schematic metric thread. Outside bevels of 30° and thread mouths of 90° are selected CAD details. Zinc coating and manufacturing tolerances are not modeled. Steel density 7,850 kg/m³; mass follows current volume.',
        'DIN 6334 ist eine Handelsbezeichnung für Verbindungsmuttern, keine Konformitätsbestätigung zu einer veröffentlichten DIN-Norm für dieses Produkt. Nennhöhe h = 3d, durchgehendes schematisches metrisches Gewinde. Außenfasen von 30° und Gewindemündungen von 90° sind gewählte CAD-Details. Verzinkung und Fertigungstoleranzen werden nicht modelliert. Stahldichte 7.850 kg/m³; Masse aus dem aktuellen Volumen.',
        'DIN 6334 est une désignation commerciale des écrous de raccordement, pas une certification selon une norme DIN publiée pour ce produit. Hauteur nominale h = 3d, filetage métrique traversant schématique. Les chanfreins extérieurs de 30° et les entrées de filetage de 90° sont des détails CAO choisis. Le zingage et les tolérances de fabrication ne sont pas modélisés. Acier de masse volumique 7 850 kg/m³ ; masse calculée par le volume actuel.',
        'DIN 6334 — торговое обозначение соединительных гаек, а не подтверждение соответствия опубликованному стандарту DIN для этого изделия. Номинальная высота h = 3d, сквозная условная метрическая резьба. Наружные фаски 30° и входы резьбы 90° — выбранные детали САПР. Цинковое покрытие и производственные допуски не моделируются. Плотность стали 7 850 кг/м³; масса вычисляется по текущему объёму.']
    data['labels']['coupling_art']=['DIN 6334 — '+x for x in ('výkres nativního modelu','native model drawing','Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
    data['sources']['coupling_aspen']=dict(label='Aspen Fasteners · nominal coupling-nut dimensions',url=rows[0]['source_url'])
    data['sources']['coupling_designation']=dict(label='Fabory · coupling-nut trade designation',url='https://www.fabory.com/nl/Bevestigingsartikelen/Moeren/Verbindingsmoeren/c/010304')
    fields=[('P','pitch_mm'),('s','across_flats_mm'),('h','height_mm'),('e_min','minimum_corners_mm')]
    headers=['coupling_d'];data['labels']['coupling_d']=['d']*5
    for i,(symbol,_) in enumerate(fields):
        key='coupling_col_'+str(i);headers.append(key);data['labels'][key]=[symbol+' [mm]']*5
    figure=json.loads((art/'illustrations.json').read_text(encoding='utf-8'));assert len(figure)==1
    stem=Path(figure[0]['part']).stem
    data['pages'].append(dict(path=path,title='coupling_title',heading='coupling_heading',headers=headers,
        rows=[['M'+format(v['diameter_mm'],'g'),*[format(v[k],'.6g') for _,k in fields]] for v in rows],
        notes=['units','coupling_scope','nut_origin_seating_note'],sources=['coupling_aspen','coupling_designation'],sections=[],
        freeze_header=True,localized_illustrations=True,figures=[dict(path=stem+'.png',caption='coupling_art',
            localized_paths={lang:stem+'_'+lang+'.png' for lang in data['languages']})]))
    return data


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('models','art','output'):parser.add_argument('--'+key,type=Path,required=True)
    args=parser.parse_args();data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    args.output.write_text(json.dumps(prepare(data,args.models,args.art),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
