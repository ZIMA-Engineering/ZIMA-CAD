"""Prepare ISO-first washer pages while preserving original CSN source cells."""
import argparse
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--art',type=Path,required=True)
    p.add_argument('--models',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    reviewed=json.loads((ROOT/'tools/data/plain-washer-iso-reviewed-dimensions.json').read_text(encoding='utf-8'))
    art={r['standard']:r for r in json.loads((args.art/'illustrations.json').read_text(encoding='utf-8')) if r['standard'].startswith('ISO')}
    models=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']
    data['labels']['iso_washer_primary_note']=[
        'Hlavní tabulky a modely používají rozměry uvedených ISO norem z roku 2000. Starší ČSN rozměry nejsou pouze přejmenované; velikosti mimo rozsah ISO jsou uvedeny samostatně.',
        'The main tables and models use dimensions from the listed 2000 ISO editions. Historic CSN sizes are not merely renamed; sizes outside the ISO range are listed separately.',
        'Haupttabellen und Modelle verwenden Maße der angegebenen ISO-Ausgaben von 2000. Historische CSN-Maße werden nicht nur umbenannt; Größen außerhalb des ISO-Bereichs stehen gesondert.',
        'Les tableaux principaux et les modèles utilisent les dimensions des éditions ISO de 2000 indiquées. Les anciennes dimensions CSN ne sont pas simplement renommées ; les tailles hors plage ISO sont séparées.',
        'Основные таблицы и модели используют размеры указанных редакций ISO 2000 года. Исторические размеры CSN не просто переименованы; размеры вне диапазона ISO указаны отдельно.']
    data['labels']['iso_washer_chamfer_note']=[
        'Model ISO 7090 má horní vnější sražení 45° do hloubky h/3; norma připouští úhel 30° až 45° a hloubku h/4 až h/2.',
        'The ISO 7090 model has an upper outside chamfer at 45° and depth h/3; the standard permits 30° to 45° and depth h/4 to h/2.',
        'Das ISO-7090-Modell hat oben außen eine Fase mit 45° und Tiefe h/3; die Norm erlaubt 30° bis 45° und h/4 bis h/2.',
        'Le modèle ISO 7090 comporte un chanfrein extérieur supérieur à 45°, de profondeur h/3 ; la norme autorise 30° à 45° et h/4 à h/2.',
        'Модель ISO 7090 имеет верхнюю наружную фаску 45° глубиной h/3; норма допускает 30°–45° и глубину h/4–h/2.']
    data['labels']['historical_washer_outside_iso']=[
        'Historické velikosti ČSN mimo rozsah ISO','Historic CSN sizes outside the ISO range','Historische CSN-Größen außerhalb des ISO-Bereichs',
        'Tailles historiques CSN hors plage ISO','Исторические размеры CSN вне диапазона ISO']
    symbols=['d','d1,min','d1,max','d2,max','d2,min','h','h,max','h,min']
    for symbol in symbols:data['labels']['washer_symbol_'+symbol]=[symbol]*5
    for key,text in [('washer_standard','Norma|Standard|Norm|Norme|Стандарт'),('washer_form','Tvar|Form|Form|Forme|Форма')]:
        data['labels'][key]=text.split('|')
    for topic in ('01-HEXAGON','02-CAP-HEAD'):
        page=next(p for p in data['pages'] if p['path']=='06-WASHERS/'+topic)
        if topic=='02-CAP-HEAD':
            data['labels'][page['title']]=[
                'Podložky pro válcové a půlkulaté hlavy',
                'Washers for cylindrical and pan heads',
                'Scheiben für Zylinder- und Flachrundköpfe',
                'Rondelles pour têtes cylindriques et bombées',
                'Шайбы под цилиндрические и полукруглые головки']
        for b in [page,*page.get('sections',[])]:
            if b.get('source_table'):b['superseded_display']=True
        # Keep only original source blocks plus the reproducible ISO sections.
        page['sections']=[s for s in page.get('sections',[]) if s.get('source_table')]
        page['figures']=[];page.update(localized_illustrations=True,figures_side_by_side=True,figures_wrap=True)
        page['notes']=[n for n in page['notes'] if n not in ('iso_washer_primary_note','iso_washer_chamfer_note')]+['iso_washer_primary_note']
        if topic=='01-HEXAGON':page['notes'].append('iso_washer_chamfer_note')
        for family in reviewed['families']:
            standard=family['standard']
            if (standard=='ISO 7092')!=(topic=='02-CAP-HEAD'):continue
            key='washer_'+standard.replace(' ','');data['labels'][key]=[standard]*5
            source_key=key+'_dimensions';data['sources'][source_key]=dict(label=standard+':2000',url=family['source'])
            if source_key not in page['sources']:page['sources'].append(source_key)
            size_keys=['nominal_mm','bore_mm','bore_max_mm','outside_mm','outside_min_mm','thickness_mm','thickness_max_mm','thickness_min_mm']
            rows=[[f'{v[k]:g}' for k in size_keys] for v in family['sizes']]
            page['sections'].append(dict(heading=key,headers=['washer_symbol_'+s for s in symbols],rows=rows,
                preferred_rows=[i for i,v in enumerate(family['sizes']) if v['preferred']],iso_washer_standard=standard))
            r=art[standard];stem=Path(r['part']).stem;caption='native_'+key
            data['labels'][caption]=[standard+' — '+t for t in ('výkres nativního modelu','native model drawing','Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
            page['figures'].append(dict(path=stem+'.png',caption=caption,localized_paths={lang:stem+'_'+lang+'.png' for lang in data['languages']}))
        historical=[v for v in models if v['standard'].startswith('ČSN') and v['family']==topic]
        if historical:
            page['sections'].append(dict(heading='historical_washer_outside_iso',
                headers=['washer_standard','washer_form','washer_symbol_d','washer_symbol_d1,min','washer_symbol_d2,max','washer_symbol_h'],
                rows=[[v['standard'],v['form'] or '—',*(f'{v[k]:g}' for k in ('nominal_mm','bore_mm','outside_mm','thickness_mm'))] for v in historical]))
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
