"""Prepare reviewed DIN/hexalobular pages and explicit spring-table corrections."""
import argparse
import json
from pathlib import Path
import uuid

ROOT=Path(__file__).resolve().parents[1]


def prepare(data,art):
    reviewed=json.loads((ROOT/'tools/data/special-cap-screw-reviewed-dimensions.json').read_text(encoding='utf-8'))
    labels=data['labels'];sources=data['sources'];languages=data['languages']
    labels['reviewed_catalogue']=['Prověřený výběr katalogových podkladů','Reviewed catalogue selection','Geprüfte Katalogauswahl','Sélection de catalogue vérifiée','Проверенная выборка каталогов']
    labels['catalogue_range_note']=[
        'Rozměry a délky vybraného katalogového rozsahu. ✓ znamená uvedenou kombinaci, nikoli aktuální skladovou dostupnost nebo úplný rozsah normy.',
        'Dimensions and lengths of the reviewed catalogue range. ✓ marks a listed combination, not current stock or the complete standard range.',
        'Maße und Längen des geprüften Katalogbereichs. ✓ kennzeichnet eine aufgeführte Kombination, keinen aktuellen Lagerbestand oder vollständigen Normumfang.',
        'Dimensions et longueurs de la gamme de catalogue vérifiée. ✓ indique une combinaison répertoriée, sans garantir le stock ni couvrir toute la norme.',
        'Размеры и длины проверенного каталожного диапазона. ✓ обозначает указанное сочетание, а не наличие на складе или полный диапазон стандарта.']
    labels['catalogue_model_note']=[
        'Tabulka uvádí rozměry nativních modelů. s je jmenovitá velikost klíče, s CAD skutečná šířka modelovaného otvoru. Výchozí pevnostní třída je uvedena samostatně.',
        'The table gives native model dimensions. s is the nominal key size; s CAD is the modeled socket width. The default property class is listed separately.',
        'Die Tabelle enthält Maße der nativen Modelle. s ist die Nennschlüsselweite, s CAD die modellierte Öffnungsweite. Die voreingestellte Festigkeitsklasse ist gesondert angegeben.',
        'Le tableau indique les dimensions des modèles natifs. s est la taille nominale de clé, s CAD la largeur modélisée de l’empreinte. La classe de qualité par défaut est indiquée séparément.',
        'В таблице приведены размеры нативных моделей. s — номинальный размер ключа, s CAD — ширина смоделированного гнезда. Класс прочности по умолчанию указан отдельно.']
    labels['hexalobular_cad_note']=[
        'Šestioblouk používá jmenovitý CAD obrys podle přílohy A ISO 10664:1999 a rozměry A/B podle ISO 10664:2014. Model nenahrazuje výrobní kontrolu kalibry.',
        'The hexalobular drive uses the nominal CAD contour of ISO 10664:1999 Annex A and A/B dimensions from ISO 10664:2014. The model does not replace manufacturing gauge inspection.',
        'Der Sechsrund verwendet die nominale CAD-Kontur nach ISO 10664:1999 Anhang A und A/B-Maße nach ISO 10664:2014. Das Modell ersetzt keine Fertigungsprüfung mit Lehren.',
        'L’empreinte hexalobulaire utilise le contour CAO nominal de l’annexe A d’ISO 10664:1999 et les dimensions A/B d’ISO 10664:2014. Le modèle ne remplace pas le contrôle de fabrication par calibres.',
        'Шестилучевой профиль использует номинальный контур САПР из приложения A ISO 10664:1999 и размеры A/B по ISO 10664:2014. Модель не заменяет производственный контроль калибрами.']
    labels['catalogue_lengths']=['Katalogové délky','Catalogue lengths','Kataloglängen','Longueurs de catalogue','Каталожные длины']
    labels['default_model_property_class']=['Výchozí pevnostní třída modelů','Default model property class','Voreingestellte Modell-Festigkeitsklasse','Classe de qualité des modèles par défaut','Класс прочности моделей по умолчанию']
    for symbol in ('d','P','dk','k','s','s CAD','t','r','b','dh','t2','A','B','T','l [mm]'):
        labels.setdefault('symbol_'+symbol,[symbol]*5)
    illustrations={r['standard']:r for r in json.loads((art/'illustrations.json').read_text(encoding='utf-8'))}
    norms={'DIN 7984':'https://www.dinmedia.de/de/norm/din-7984/349413305',
        'DIN 6912':'https://www.dinmedia.de/de/norm/din-6912/331996195',
        'ISO 14579':'https://www.iso.org/standard/56455.html'}
    for family in reviewed['families']:
        standard=family['standard'];token=standard.replace(' ','').lower();path='04-SCREWS/04-CAP-HEAD/'+family['directory']
        title='directory_'+path.replace('/','_');labels[title]=list(family['names'])
        source_keys=[]
        for suffix,url,label in [('standard',norms[standard],standard),('dimensions',family['dimension_source'],standard+' · catalogue dimensions'),
                ('lengths',family['availability_source'],standard+' · catalogue lengths')]:
            key=token+'_'+suffix;sources[key]=dict(label=label,url=url);source_keys.append(key)
        if standard=='ISO 14579':
            for suffix,url,label in [('cad',reviewed['contour_source'],'ISO 10664:1999 · Annex A'),
                    ('ab',reviewed['socket_dimensions_source'],'ISO 10664:2014 · A/B')]:
                key=token+'_'+suffix;sources[key]=dict(label=label,url=url);source_keys.append(key)
        artifact=illustrations[standard];stem=Path(artifact['drawing']).stem;caption='native_reference_'+token
        labels[caption]=[standard+' — '+text for text in ('výkres nativního modelu','native model drawing','Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
        symbols=['d','P','dk','k']+(['s','s CAD','t','r','b'] if standard.startswith('DIN') else ['T','A','B','t','r'])
        if standard=='DIN 6912':symbols+=['dh','t2']
        rows=[]
        for size in family['sizes']:
            values={'d':'M'+f'{size["d"]:g}'.replace('.',','),'P':size['P'],'dk':size['dk'],'k':size['k'],'t':size['t'],'r':size['r']}
            if 's' in size:values.update(s=round(size['s']*2)/2,**{'s CAD':size['s'],'b':size['b']})
            else:values.update(T='T'+str(size['socket_no']),A=size['A'],B=size['B'])
            if 'dh' in size:values.update(dh=size['dh'],t2=size['t2'])
            rows.append([str(values[key]) if isinstance(values[key],str) else f'{values[key]:g}' for key in symbols])
        lengths=sorted({length for size in family['sizes'] for length in size['catalogue_lengths']})
        threads=['M'+f'{size["d"]:g}'.replace('.',',') for size in family['sizes']]
        for thread in threads:labels.setdefault('symbol_'+thread,[thread]*5)
        page=dict(path=path,title=title,id=str(uuid.uuid5(uuid.NAMESPACE_URL,'zima-cad:special-fasteners/'+path)),
            headers=['item','value'],rows=[[{'key':'reference'},standard],[{'key':'default_model_property_class'},'08.8' if standard.startswith('DIN') else '8.8']],
            notes=['units','catalogue_range_note','catalogue_model_note']+(['hexalobular_cad_note'] if standard=='ISO 14579' else []),
            sources=source_keys,review='reviewed_catalogue',freeze_header=True,omit_approximate_mass=True,localized_illustrations=True,
            figures=[dict(path=stem+'.png',caption=caption,localized_paths={lang:stem+'_'+lang+'.png' for lang in languages})],
            sections=[dict(headers=['symbol_'+key for key in symbols],rows=rows),
                dict(heading='catalogue_lengths',headers=['symbol_l [mm]',*['symbol_'+s for s in threads]],
                    rows=[[f'{length:g}',*['✓' if length in size['catalogue_lengths'] else '—' for size in family['sizes']]] for length in lengths])])
        data['pages']=[p for p in data['pages'] if p['path']!=path];data['pages'].append(page)
    spring=next(p for p in data['pages'] if p['path']=='06-WASHERS/03-SPRING')
    labels['spring_compressed_model_note']=[
        'Tabulkové výšky h1/h2 popisují volný stav. Knihovní modely jsou stlačené pro usazení v sestavě; řez je vedený v rovině osy, pod 45°, s rovnoběžnými stěnami a kolmou mezerou 1 mm.',
        'Tabulated heights h1/h2 describe the free state. Library models are compressed for assembly seating; the axial cut has parallel 45° walls with a 1 mm perpendicular gap.',
        'Die Tabellenhöhen h1/h2 beschreiben den freien Zustand. Bibliotheksmodelle sind zum Einbau zusammengedrückt; der axiale Schnitt hat parallele 45°-Wände mit 1 mm senkrechtem Abstand.',
        'Les hauteurs h1/h2 du tableau décrivent l’état libre. Les modèles de bibliothèque sont comprimés pour le montage ; la coupe axiale comporte deux parois parallèles à 45°, espacées perpendiculairement de 1 mm.',
        'Табличные высоты h1/h2 относятся к свободному состоянию. Модели библиотеки сжаты для установки в сборке; осевой разрез имеет параллельные стенки под 45° с перпендикулярным зазором 1 мм.']
    if 'spring_compressed_model_note' not in spring['notes']:spring['notes'].append('spring_compressed_model_note')
    source_url='https://encyklopediapoznania.sk/data/eknihy/strojarenstvo/strojnicke_tabulky_z_roku_1965.pdf'
    sources['spring_historical_scan']=dict(label='ČSN 02 1740 · historical table, p. 183',url=source_url)
    if 'spring_historical_scan' not in spring['sources']:spring['sources'].append('spring_historical_scan')
    table='06-WASHERS/03-SPRING#0'
    for index,column,before,after in [(7,4,'2 ±0,15','2,5 ±0,15'),(14,3,'31,5','35,5')]:
        row=data['curated_source_tables'][table]['rows'][index];assert row[column] in (before,after);row[column]=after
        for block in [spring,*spring.get('sections',[])]:
            if block.get('source_table')==table:
                local=index-block['source_rows'][0]
                if 0<=local<len(block['rows']) and column in block['source_columns']:
                    block['rows'][local][block['source_columns'].index(column)]=after
        correction=dict(source_table=table,row=index,column=column,before=before,after=after,source=source_url)
        if correction not in data['reviewed_numeric_corrections']:data['reviewed_numeric_corrections'].append(correction)
    return data


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--art',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    args.output.write_text(json.dumps(prepare(data,args.art),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
