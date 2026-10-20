"""Add reviewed ISO lock-nut rows while preserving the separate archived CSN table."""
import argparse
import copy
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LANGUAGES=('cs','en','de','fr','ru')


def prepare(data,models,art):
    rows=json.loads((models/'manifest.json').read_text(encoding='utf-8'))['variants'];assert len(rows)==13
    figures=json.loads((art/'illustrations.json').read_text(encoding='utf-8'));assert len(figures)==1
    page=next(p for p in data['pages'] if p['path']=='05-NUTS/07-LOCKING')
    archived=copy.deepcopy(page if page.get('source_table')=='05-NUTS/07-LOCKING#0' else
        next(s for s in page['sections'] if s.get('source_table')=='05-NUTS/07-LOCKING#0'))
    for key in ('path','id','title','sections','freeze_header','thread_dimension_rows','display_row_count'):archived.pop(key,None)
    data['labels']['insert_lock_archived']=['ČSN 02 1492 · archivní rozměry, samostatný podklad',
        'CSN 02 1492 · archived dimensions, separate reference','CSN 02 1492 · historische Maße, separate Referenz',
        'CSN 02 1492 · dimensions archivées, référence distincte','CSN 02 1492 · архивные размеры, отдельный источник']
    archived['heading']='insert_lock_archived'
    data['labels']['insert_lock_heading']=['ISO 10511:2012 · '+x for x in ('nízké matice','thin nuts','niedrige Muttern','écrous bas','низкие гайки')]
    data['labels']['insert_lock_scope']=[
        'Modely M3–M36 vycházejí z přezkoumané tabulky ISO 10511:2012, nikoli z technicky změněného vydání 2025 ani z odlišných archivních rozměrů ČSN. Jeden ocelový Part; vložka je symbolické modré těleso a celý objem se pro hmotnost počítá jako ocel 7 850 kg/m³. Nejde o skutečnou hmotnost výrobku ocel/PA6. Límec, zápich a přesah vložky jsou zvolené CAD detaily; deformace a pojistný moment se nesimulují. Závit je schematický. Relations vážou vložku na šířku, výšku a zvolený závit matice. Sražení ústí zůstává samostatně upravitelné.',
        'The M3–M36 models use the reviewed ISO 10511:2012 table, not the technically revised 2025 edition or the different archived CSN dimensions. One steel Part; the insert is a symbolic blue Body and all volume uses steel density 7,850 kg/m³ for mass. This is not the physical steel/PA6 product mass. Collar, recess and insert interference are selected CAD details; deformation and prevailing torque are not simulated. Threads are schematic. Relations connect the insert to nut width, height and selected thread. The mouth chamfer remains independently editable.',
        'Die Modelle M3–M36 verwenden die geprüfte Tabelle ISO 10511:2012, nicht die technisch überarbeitete Ausgabe 2025 oder abweichende historische CSN-Maße. Ein Stahl-Part; der Einsatz ist ein symbolischer blauer Körper und das gesamte Volumen wird mit 7.850 kg/m³ als Stahl berechnet. Dies ist nicht die tatsächliche Masse des Stahl/PA6-Produkts. Bund, Vertiefung und Einsatzübermaß sind gewählte CAD-Details; Verformung und Sicherungsmoment werden nicht simuliert. Schematische Gewinde. Relations koppeln den Einsatz an Breite, Höhe und gewähltes Gewinde. Die Mündungsfase bleibt unabhängig editierbar.',
        'Les modèles M3–M36 utilisent le tableau vérifié ISO 10511:2012, pas l’édition 2025 révisée ni les dimensions CSN archivées différentes. Un Part en acier ; l’insert est un corps bleu symbolique et tout le volume est calculé en acier à 7 850 kg/m³. Ce n’est pas la masse physique du produit acier/PA6. Collerette, logement et serrage de l’insert sont des détails CAO choisis ; déformation et couple de freinage ne sont pas simulés. Filetages schématiques. Les Relations lient l’insert à la largeur, à la hauteur et au filetage choisi. Le chanfrein d’entrée reste modifiable séparément.',
        'Модели M3–M36 используют проверенную таблицу ISO 10511:2012, а не технически пересмотренное издание 2025 или отличающиеся архивные размеры CSN. Один стальной Part; вставка — условное синее тело, весь объём рассчитывается как сталь плотностью 7 850 кг/м³. Это не физическая масса изделия сталь/PA6. Буртик, выемка и натяг вставки — выбранные детали САПР; деформация и стопорящий момент не моделируются. Резьба условная. Relations связывают вставку с шириной, высотой и выбранной резьбой гайки. Входная фаска остаётся независимо редактируемой.']
    fields=[('P','pitch_mm'),('s','across_flats_mm'),('h max','height_mm'),('m min','minimum_thread_height_mm'),
        ("m′ min",'minimum_wrench_height_mm'),('dw min','minimum_bearing_diameter_mm'),('da max','maximum_mouth_diameter_mm')]
    headers=['insert_lock_d'];data['labels']['insert_lock_d']=['d']*5
    for i,(symbol,_) in enumerate(fields):
        key='insert_lock_col_'+str(i);headers.append(key);data['labels'][key]=[symbol+' [mm]']*5
    source='insert_lock_2012';data['sources'][source]=dict(label='ISO 10511:2012 · Table 1',url=json.loads((ROOT/'tools/data/insert-lock-nut-reviewed-dimensions.json').read_text(encoding='utf-8'))['source_url'])
    stem=Path(figures[0]['part']).stem;data['labels']['insert_lock_art']=['ISO 10511:2012 — '+x for x in ('výkres nativního modelu','native model drawing','Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
    identity={k:page[k] for k in ('path','title','id')};page.clear();page.update(identity)
    page.update(heading='insert_lock_heading',headers=headers,
        rows=[['M'+format(v['diameter_mm'],'g'),*[format(v[k],'.6g') for _,k in fields]] for v in rows],
        notes=['units','insert_lock_scope'],sources=[source],sections=[archived],freeze_header=True,omit_approximate_mass=True,
        thread_dimension_rows=True,display_row_count=len(rows)+len(archived['headers'])-1,
        localized_illustrations=True,figures=[dict(path=stem+'.png',caption='insert_lock_art',
            localized_paths={lang:stem+'_'+lang+'.png' for lang in LANGUAGES})])
    return data


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('models','art','output'):p.add_argument('--'+key,type=Path,required=True)
    args=p.parse_args();data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    args.output.write_text(json.dumps(prepare(data,args.models,args.art),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
