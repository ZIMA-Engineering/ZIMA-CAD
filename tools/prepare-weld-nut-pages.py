"""Publish reviewed weld-nut tables while retaining the retired source dataset."""
import argparse
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT/'tools/data/fastener-tables.json'
LANGUAGES=('cs','en','de','fr','ru')


def prepare(data,stages,art):
    parent=next(p for p in data['pages'] if p['path']=='05-NUTS/05-WELD')
    if parent.get('source_table'):
        assert not any(p['path']==parent['path'] for p in data.get('retired_pages',[]))
        data.setdefault('retired_pages',[]).append(parent.copy())
    replacement={k:parent[k] for k in ('path','title','id')}
    replacement.update(headers=['symbol','meaning'],rows=[],notes=[],sources=[],sections=[])
    parent.clear();parent.update(replacement)
    data['labels']['weld_thread']=['Závit','Thread','Gewinde','Filetage','Резьба']
    data['labels']['weld_contact']=[
        'Dosedací plochy tří výstupků leží na XY v Z = 0. Osa závitu prochází počátkem; matice směřuje do +Z. Model představuje stav před svařením.',
        'The three projection contact faces lie on XY at Z = 0. The thread axis passes through the Origin; the nut extends in +Z. The model represents the unwelded state.',
        'Die drei Vorsprungkontaktflächen liegen auf XY bei Z = 0. Die Gewindeachse geht durch den Ursprung; die Mutter erstreckt sich in +Z. Das Modell zeigt den ungeschweißten Zustand.',
        'Les trois faces de contact des bossages sont sur XY à Z = 0. L’axe du filetage passe par l’origine ; l’écrou s’étend vers +Z. Le modèle représente l’état avant soudage.',
        'Три контактные поверхности выступов лежат в XY при Z = 0. Ось резьбы проходит через начало координат; гайка направлена вдоль +Z. Модель показывает состояние до сварки.']
    data['labels']['weld_nominal']=[
        'Jmenovitá geometrie CAD se schematickým závitem a ocelí o hustotě 7 850 kg/m³. Hmotnost modelu se počítá z objemu; nejde o osvědčení materiálu ani svařitelnosti.',
        'Nominal CAD geometry with schematic thread and steel density 7,850 kg/m³. Model mass is calculated from volume; this is not a material or weldability certificate.',
        'CAD-Nenngeometrie mit schematischem Gewinde und Stahldichte 7.850 kg/m³. Die Modellmasse wird aus dem Volumen berechnet; dies ist kein Werkstoff- oder Schweißeignungsnachweis.',
        'Géométrie CAO nominale avec filetage schématique et acier de masse volumique 7 850 kg/m³. La masse est calculée à partir du volume ; ce n’est pas un certificat de matériau ou de soudabilité.',
        'Номинальная геометрия САПР со схематической резьбой и плотностью стали 7 850 кг/м³. Масса вычисляется по объёму; это не сертификат материала или свариваемости.']
    data['labels']['weld_iso_height']=[
        'Výška m nezahrnuje výstupky c; celková výška je m + c. Zaoblení kořene R = 0,5 mm a zadní sražení 30° jsou zvolené detaily modelu.',
        'Height m excludes projections c; overall height is m + c. The root radius R = 0.5 mm and back bevel of 30° are chosen model details.',
        'Die Höhe m schließt die Vorsprünge c aus; die Gesamthöhe ist m + c. Wurzelradius R = 0,5 mm und rückseitige Fase 30° sind gewählte Modelldetails.',
        'La hauteur m exclut les bossages c ; la hauteur totale est m + c. Le rayon de raccordement R = 0,5 mm et le chanfrein arrière de 30° sont des détails de modèle choisis.',
        'Высота m не включает выступы c; общая высота равна m + c. Радиус сопряжения R = 0,5 мм и задняя фаска 30° — выбранные детали модели.']
    data['labels']['weld_din_height']=[
        'Výška m zahrnuje přivařovací výstupky h1. h2 je výška středicího osazení. Zadní sražení 30° je zvolený detail modelu.',
        'Height m includes the weld projections h1. h2 is the centering pilot height. The back bevel of 30° is a chosen model detail.',
        'Die Höhe m schließt die Schweißvorsprünge h1 ein. h2 ist die Zentrierbundhöhe. Die rückseitige Fase 30° ist ein gewähltes Modelldetail.',
        'La hauteur m inclut les bossages de soudage h1. h2 est la hauteur du pilote de centrage. Le chanfrein arrière de 30° est un détail de modèle choisi.',
        'Высота m включает сварочные выступы h1. h2 — высота центрирующего буртика. Задняя фаска 30° — выбранная деталь модели.']
    for stage,references in zip(stages,art):
        variants=json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants']
        figure=json.loads((references/'illustrations.json').read_text(encoding='utf-8'))[0]
        iso=variants[0]['kind']=='iso';key='weld_iso' if iso else 'weld_din';standard=variants[0]['standard']
        data['labels'][key]=[variants[0]['names'][lang] for lang in LANGUAGES]
        data['labels'][key+'_art']=[standard+' — '+s for s in ('výkres nativního modelu','native model drawing','Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
        fields=[('P','pitch_mm'),('s','across_flats_mm'),('m','height_mm')]
        fields+=([('da','mouth_diameter_mm'),('dc','flange_diameter_mm'),('b','projection_depth_mm'),('c','projection_height_mm'),
            ('f','flange_height_mm'),('g','projection_width_mm'),('r1','projection_radius_mm'),('r2','flange_radius_mm')]
            if iso else [('d2','pilot_diameter_mm'),('h1','projection_height_mm'),('h2','pilot_height_mm')])
        for symbol,_ in fields:data['labels']['weld_'+symbol]=[symbol+' [mm]']*5
        data['sources'][key]=dict(label=standard+(' · ISO 21670:2014' if iso else ' · Bollhoff dimensions'),
            url='https://www.iso.org/standard/63369.html' if iso else 'https://eshop-sk.boellhoff.com/out/media/pdf/DIN_929_Stahl___en.pdf')
        threads=[]
        for v in variants:
            thread='M'+f'{v["diameter_mm"]:g}'+'×'+f'{v["pitch_mm"]:g}'
            label='weld_thread_'+thread;data['labels'][label]=[thread]*5;threads.append(label)
        rows=[[symbol,*[f'{v[field]:g}' for v in variants]] for symbol,field in fields]
        stem=Path(figure['part']).stem
        page=dict(path='05-NUTS/'+variants[0]['family'],title=key,
            headers=['parameter',*threads],rows=rows,
            notes=['units','weld_contact','weld_nominal','weld_iso_height' if iso else 'weld_din_height'],
            sources=[key],sections=[],review='reviewed_catalogue',freeze_header=True,display_row_count=len(variants),
            thread_dimension_rows=True,localized_illustrations=True,figures=[dict(path=stem+'.png',caption=key+'_art',
                localized_paths={lang:stem+'_'+lang+'.png' for lang in LANGUAGES})])
        previous=next((p for p in data['pages'] if p['path']==page['path']),None)
        if previous:previous.clear();previous.update(page)
        else:data['pages'].append(page)
    return data


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--din-models',type=Path,required=True);parser.add_argument('--iso-models',type=Path,required=True)
    parser.add_argument('--din-art',type=Path,required=True);parser.add_argument('--iso-art',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=DATA);args=parser.parse_args()
    data=prepare(json.loads(DATA.read_text(encoding='utf-8')),[args.din_models,args.iso_models],[args.din_art,args.iso_art])
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
