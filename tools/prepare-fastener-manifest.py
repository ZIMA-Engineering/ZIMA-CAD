"""Prepare a reviewable native-model manifest, never infer geometry from damaged cells."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FAMILIES = {
    '01-GRADE-AB': ('ISO 4014', False, False),
    '02-GRADE-C': ('ISO 4016', False, True),
    '03-FULL-AB': ('ISO 4017', True, False),
    '04-FULL-C': ('ISO 4018', True, True),
    '05-FINE-AB': ('ISO 8765', False, False),
    '06-FULL-FINE': ('ISO 8676', True, False),
}
NAMES = {
    True: dict(cs='Šroub 6HR se závitem až k hlavě', en='Hexagon head screw, full thread',
        de='Sechskantschraube mit Vollgewinde', fr='Vis à tête hexagonale à filetage intégral',
        ru='Болт с шестигранной головкой с полной резьбой'),
    False: dict(cs='Šroub 6HR s částečným závitem', en='Hexagon head bolt, partial thread',
        de='Sechskantschraube mit Teilgewinde', fr='Vis à tête hexagonale partiellement filetée',
        ru='Болт с шестигранной головкой с неполной резьбой'),
}
LENGTHS = [3,4,5,6,8,10,12,16,20] + list(range(25,101,5)) + list(range(110,201,10)) + list(range(220,501,20))


def prepare():
    geometry = json.loads((ROOT / 'tools/data/hex-fastener-geometry.json').read_text(encoding='utf-8'))
    rows = {row[0]: row for row in geometry['sizes']}
    tables = json.loads((ROOT / 'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    variants = []
    corrections = list(tables.get('reviewed_header_corrections', []))
    for family, (standard, full, grade_c) in FAMILIES.items():
        page = next(p for p in tables['pages'] if p['path'] == '04-SCREWS/01-HEX-HEAD/' + family)
        fine = family in ('05-FINE-AB', '06-FULL-FINE')
        sizes = set()
        for section in [page] + page.get('sections', []):
            if section['headers'][0] != 'parameter':
                continue
            for header in section['headers'][1:]:
                match = re.fullmatch(r'symbol_\(?M([\d,]+)(?:×([\d,]+))?\)?', header)
                if not match:
                    raise ValueError(f'Unreviewed size header: {header}')
                d = float(match[1].replace(',', '.'))
                pitch = float(match[2].replace(',', '.')) if match[2] else rows[d][1]
                if standard == 'ISO 8765' and d == 12 and pitch == 1:
                    raise ValueError('Uncorrected historical M12 fine-thread header')
                sizes.add((d, pitch))
        for d, pitch in sorted(sizes):
            _, coarse, flats, head, radius, bearing_a, bearing_bc = rows[d]
            if grade_c and d < 5:
                raise ValueError('Grade C starts at M5')
            grades = ['C'] if grade_c else (['A', 'B'] if 16 <= d <= 24 else ['A'] if d < 16 else ['B'])
            for grade in grades:
                if full:
                    desired = {3:20,4:20,5:20,6:25}.get(d, min(4*d,200))
                else:
                    short_b = {1.6:9,2:10,2.5:11,3:12,3.5:13,4:14}.get(d, 2*d+6)
                    desired = max(5*d, short_b + 5*coarse + max(2*coarse, radius))
                if grade == 'B' and d <= 24:
                    desired = max(desired, 160)
                length = next(value for value in LENGTHS if value >= desired)
                if length > min(10*d, 200 if full else 500):
                    raise ValueError(f'Nonstandard length {standard} M{d:g} {grade}: {length}')
                if grade == 'A' and (d > 24 or length > min(10*d,150)):
                    raise ValueError('Grade A length/diameter limit exceeded')
                bearing_min = bearing_a if grade == 'A' else bearing_bc
                c_max = 0.25 if d <= 2.5 else 0.4 if d <= 4 else 0.5 if d <= 6 else 0.6 if d <= 14 else 0.8 if d <= 36 else 1
                c_min = 0.1 if d <= 2.5 else 0.15 if d <= 14 else 0.2 if d <= 36 else 0.3
                designation = 'M' + f'{d:g}'.replace('.', ',')
                if fine:
                    designation += '×' + f'{pitch:g}'.replace('.', ',')
                size_name = f'M{d:g}' + (f'x{pitch:g}' if fine else '') + f'x{length:g}'
                prefix = 'HHS' if full else 'HHB'
                suffix = '' if standard == 'ISO 4017' and grade == 'A' and d in (3,4,5,6) else '-'+grade
                variant = dict(filename=f'{prefix}-{standard.replace(" ", "")}-{size_name}{suffix}.prtz',
                    family=family, standard=standard, product_grade=grade, diameter_mm=d, pitch_mm=pitch,
                    coarse_pitch_mm=coarse, designation=designation, fine_pitch=fine, length_mm=length,
                    across_flats_mm=flats, head_height_mm=head, underhead_radius_mm=radius,
                    bearing_diameter_mm=(bearing_min+flats)/2, bearing_height_mm=(c_min+c_max)/2,
                    property_class='4.6' if grade_c else '8.8', names=NAMES[full],
                    tip_chamfer=dict(mode='equal_distance',distance_a_mm=.7*pitch,angle_degrees=45,flip=False))
                if not full:
                    b = {1.6:9,2:10,2.5:11,3:12,3.5:13,4:14}.get(d,
                        2*d + (6 if length <= 125 else 12 if length <= 200 else 25))
                    variant.update(thread_length_mm=b, runout_mm=5*coarse,
                        minimum_unthreaded_shank_mm=length-b-5*coarse)
                    if variant['minimum_unthreaded_shank_mm'] <= 0:
                        raise ValueError('Thread runout reaches the head')
                variants.append(variant)
    # Historical small-head standards have their own geometry and quality
    # terminology. Do not assign modern ISO product grades A/B/C to them.
    small = [
        (6,1,9,4,.3,11,1.6,1,2.5,None,None),
        (7,1,11,4.5,.3,13,1.6,1,2.5,None,None),
        (8,1.25,12,5,.5,14,1.8,1.4,3.2,1,2.5),
        (10,1.5,14,6,.5,16,1.8,1.6,3.8,1.25,3.2),
        (12,1.75,17,7,1,18,2,1.6,4.3,1.25,3.2),
        (14,2,19,8,1,20,2,1.6,5,1.5,3.8),
        (16,2,22,9,1,22,3,2,5,1.5,3.6),
    ]
    for number,full,hole in ((1201,False,False),(1205,False,True),(1207,True,False),(1208,True,True)):
        for d,coarse,flats,head,radius,b,hole_d,z,runout,fine_pitch,fine_runout in small:
            for pitch in (coarse,fine_pitch) if fine_pitch else (coarse,):
                fine = pitch != coarse
                length = next(value for value in [12,14,16,18,20,22,25,28,30,32,35,38,40,42,45,48,50,55,60,65,70,75,80]
                    if value >= (2*d if full else 5*d))
                designation = f'M{d}' + ('×'+f'{pitch:g}'.replace('.', ',') if fine else '')
                size_name = f'M{d}' + (f'x{pitch:g}' if fine else '') + f'x{length}'
                names = dict(NAMES[full])
                names.update(cs='Přesný šroub s malou šestihrannou hlavou',
                    en='Precision bolt with small hexagon head', de='Präzisionsschraube mit kleinem Sechskantkopf',
                    fr='Vis de précision à petite tête hexagonale', ru='Болт повышенной точности с уменьшенной шестигранной головкой')
                thread_names = dict(cs='plný závit' if full else 'částečný závit',
                    en='full thread' if full else 'partial thread',
                    de='Vollgewinde' if full else 'Teilgewinde',
                    fr='filetage intégral' if full else 'filetage partiel',
                    ru='полная резьба' if full else 'неполная резьба')
                hole_names = dict(cs='pojistný otvor',en='locking hole',de='Sicherungsbohrung',
                    fr='trou de freinage',ru='стопорное отверстие')
                for language in names:
                    names[language] += ', '+thread_names[language]+(', '+hole_names[language] if hole else '')
                variant = dict(filename=f'{"HHS" if full else "HHB"}-CSN02{number}-{size_name}.prtz',
                    family='07-SMALL-HEX', standard=f'ČSN 02 {number}', product_grade='precision',
                    product_grade_values=dict(cs='Přesné',en='Precision',de='Präzision',fr='Précision',ru='Повышенная точность'),
                    diameter_mm=d, pitch_mm=pitch, coarse_pitch_mm=coarse, designation=designation,
                    fine_pitch=fine, length_mm=length, across_flats_mm=flats, head_height_mm=head,
                    underhead_radius_mm=radius, bearing_diameter_mm=(d+flats)/2,bearing_height_mm=.2,
                    bearing_face=False, head_chamfer_degrees=15,
                    tip_chamfer=dict(mode='distance_angle',distance_a_mm=z,angle_degrees=30,flip=True),
                    property_class='8.8',names=names)
                if hole:variant['locking_hole_diameter_mm']=hole_d
                if not full:
                    x = fine_runout if fine else runout
                    variant.update(thread_length_mm=b,runout_mm=x,minimum_unthreaded_shank_mm=length-b-x)
                    if length-b-x<=radius:raise ValueError('Small-head thread reaches the head')
                variants.append(variant)
    sources = geometry['sources'] | {
        'CSN 02 1201': 'https://nahledy.normy.biz/n.php?i=220',
        'CSN 02 1205': 'https://nahledy.normy.biz/n.php?i=222',
        'CSN 02 1207': 'https://nahledy.normy.biz/n.php?i=223',
        'CSN 02 1208': 'https://nahledy.normy.biz/nahled.php?i=224'}
    return dict(sources=sources, catalog_corrections=corrections, variants=variants)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/fastener-manifest.json')
    args = parser.parse_args()
    manifest = prepare()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'variants': len(manifest['variants']), 'catalog_corrections': manifest['catalog_corrections']}))


if __name__ == '__main__':
    main()
