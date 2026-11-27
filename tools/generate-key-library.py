"""Author nominal DIN form-A keys through the current native command host.

Candidates and command receipts remain in the requested staging directory.
The stock relation follows actual dimensions; models do not rename themselves.
Density is the typical C45 value published by Ovako (7800 kg/m3), not a
material certificate. Parallel-key perimeter chamfers follow the reviewed
manufacturer r1 minimum; Woodruff models retain their nominal sharp outline.
"""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_authoring', ROOT/'tools/generate-fastener-library.py')
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)
SEATING_COLOR = '#D5A24C'


def seating_face(native, feature):
    faces = [face for face in native.run('appearance.faces')['items']
             if face['owner'] == feature and face['key'].startswith('start:')]
    assert len(faces) == 1, faces
    surface = native.run('reference.get', kind='face', **faces[0], limit=1)['surface']
    assert surface['kind'] == 'plane' and abs(surface['origin'][2]) < 1e-9, surface
    normal = surface['axis'][2] * (-1 if surface['reversed'] else 1)
    assert normal < -1 + 1e-9, surface
    return faces[0]


def verify_seating_color(native, feature):
    face = seating_face(native, feature)
    groups = native.run('appearance.get')['groups']
    assert len(groups) == 1 and groups[0]['id'] == 'key-seating-face', groups
    assert groups[0]['faces'] == [face] and groups[0]['style']['color'] == SEATING_COLOR, groups
    return face


def variants(retaining_only=False):
    catalogs = json.loads((ROOT/'resources/data/mech/keyways.json').read_text(encoding='utf-8'))['catalogues']
    result = []
    for catalog in catalogs:
        if retaining_only:
            if catalog['id'] != 'zima-csn-parallel':
                continue
            for row in catalog['rows']:
                retaining = row.get('retaining')
                if not retaining:
                    continue
                for count, interval_key, standard in ((1, 'one_lengths', 'CSN022575'), (2, 'two_lengths', 'CSN022570')):
                    interval = retaining[interval_key]
                    if not interval:
                        continue
                    b, h = row['width'], row['height']
                    length = interval[0]
                    result.append(dict(filename=f'PK-{standard}-{b:g}x{h:g}x{length:g}.prtz',
                        woodruff=False, width=b, height=h, length=length, standard=standard,
                        edge_chamfer=0, retaining_count=count, retaining=retaining,
                        provenance=catalog['source'], length_interval=interval))
            continue
        if catalog['id'] not in ('din6885-1-maedler', 'din6888-a-i', 'din6888-a-ii'):
            continue
        for row in catalog['rows']:
            disc = catalog['type'] == 'woodruff'
            b, h = row['width'], row['height']
            # One nominal length per parallel-key section; the native length
            # remains editable. The table's permitted interval is authoritative.
            lengths = row.get('plain_lengths')
            if not disc:
                # Preferred stocked lengths (Ganter DIN 6885 form A), inside
                # the section intervals published by Wernecke.
                preferred = [6,8,10,12,14,16,18,20,22,25,28,32,36,40,45,50,56,63,70,80,90,100,110,125,140,160,180,200]
                assert lengths, row
                target = 45 if b == 10 else max(3*b, 10)
                length = min((n for n in preferred if lengths[0] <= n <= lengths[1]), key=lambda n: abs(n-target))
            else:
                length = row['disc_diameter']
            standard = 'DIN6888-A' if disc else 'DIN6885-1-A'
            filename = f'{"WK" if disc else "PK"}-{standard}-{b:g}x{h:g}x{length:g}.prtz'
            if any(v['filename'] == filename for v in result):
                continue
            result.append(dict(filename=filename, woodruff=disc, width=b, height=h,
                               length=length, standard=standard, edge_chamfer=row.get('key_edge_radius',0)))
    return result


def retaining_volume(row):
    hole = row.get('retaining')
    if not hole:
        return 0.
    return row['retaining_count'] * math.pi / 4 * (
        hole['key_clearance_diameter']**2 * row['height'] +
        (hole['key_counterbore_diameter']**2-hole['key_clearance_diameter']**2) *
        hole['key_counterbore_depth'])


def author_retaining_holes(native, row):
    """Separate clearance/counterbore cuts; their positions follow the key dimensions."""
    if not row.get('retaining_count'):
        return []
    b, h, length = row['width'], row['height'], row['length']
    data = row['retaining']
    centers = [(length-b)/2] if row['retaining_count']==1 else [b/2, length-1.5*b]
    result=[]
    for counterbore in (False, True):
        sketch=native.run('sketch.create', name='Counterbores' if counterbore else 'Clearance holes', plane='XY')['sketch']
        diameter=data['key_counterbore_diameter' if counterbore else 'key_clearance_diameter']
        positions=[]
        for x in centers:
            circle=native.run('sketch.circle.create',sketch=sketch,center=[x,0],radius_mm=diameter/2)['geometry']
            point=entity(native,sketch,circle)['center']
            native.run('sketch.constraint.create',sketch=sketch,kind='point_on_line',points=[point],geometry=['sketch_axis:x'])
            position=native.run('sketch.dimension.create',sketch=sketch,kind='distance_x',value=x,points=[point],geometry=['sketch_axis:y'])['dimension']
            native.run('sketch.dimension.create',sketch=sketch,kind='diameter',value=diameter,geometry=[circle])
            positions.append(position)
        status=native.run('sketch.solve_status',sketch=sketch)
        assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
        depth=data['key_counterbore_depth'] if counterbore else h
        side=dict(operation='extrude',length=depth,extrusion_extent='length',angle_degrees=90,rotation_extent='angle',draft_angle_degrees=0,targets=[])
        cut=native.run('feature.create',sketch=sketch,combine='subtract',parameters=dict(
            profile_plane_offset=h-depth if counterbore else 0,sides=[side,{**side,'operation':'none'}]))['container']
        result.append(dict(sketch=sketch,feature=cut,positions=positions,counterbore=counterbore))
    return result


def entity(native, sk, item):
    return native.run('sketch.entity.get', sketch=sk, entity=item)['entity']


def author(native, row, path):
    native.run('new', type='part', name=path.stem)
    sk = native.run('sketch.create', name='Profile', plane='XY')['sketch']
    b, h, length = row['width'], row['height'], row['length']
    def curve(command, **values):
        return native.run(command, sketch=sk, **values)['geometry']
    def relation(kind, **values):
        return native.run('sketch.constraint.create', sketch=sk, kind=kind, **values)
    def dimension(kind, value, **values):
        return native.run('sketch.dimension.create', sketch=sk, kind=kind, value=value, **values)['dimension']
    if not row['woodruff']:
        r = b/2
        first = curve('sketch.arc.create', center=[0, 0], start=[0, r], end=[0, -r])
        last = curve('sketch.arc.create', center=[length-b, 0], start=[length-b, -r], end=[length-b, r])
        top = curve('sketch.segment.create', first=[0, r], second=[length-b, r])
        bottom = curve('sketch.segment.create', first=[0, -r], second=[length-b, -r])
        for arc in (first, last):
            for wall in (top, bottom):
                relation('tangent', geometry=[arc, wall])
        relation('equal_radius', geometry=[first, last])
        relation('horizontal', geometry=[top])
        first_data, last_data = entity(native, sk, first), entity(native, sk, last)
        relation('point_reference', points=[first_data['center'], 'sketch_origin'])
        width = dimension('distance_y', b, points=[first_data['end'], first_data['start']])
        tips = []
        for arc, x in ((first, -r), (last, length-r)):
            tip = native.run('sketch.point.create', sketch=sk, position=[x, 0], construction=True)['point']
            relation('point_on_circle', points=[tip], geometry=[arc])
            relation('point_on_line', points=[tip], geometry=['sketch_axis:x'])
            tips.append(tip)
        extent = dimension('distance_x', length, points=tips)
        extrusion = h
        expected_volume = (b*(length-b)+math.pi*b*b/4)*h
    else:
        r = length/2
        ordinate = h-r
        half_chord = math.sqrt(r*r-ordinate*ordinate)
        arc = curve('sketch.arc.create', center=[0, 0], start=[-half_chord, ordinate], end=[half_chord, ordinate])
        top = curve('sketch.segment.create', first=[-half_chord, ordinate], second=[half_chord, ordinate])
        data = entity(native, sk, arc)
        relation('point_reference', points=[data['center'], 'sketch_origin'])
        relation('horizontal', geometry=[top])
        width = dimension('diameter', length, geometry=[arc])
        extent = dimension('distance_y', ordinate, points=[data['start']], geometry=['sketch_axis:x'])
        extrusion = b
        expected_volume = (r*r*math.acos((r-h)/r)-(r-h)*half_chord)*b
    solve = native.run('sketch.solve_status', sketch=sk)
    assert solve['maximum_residual'] < 1e-8 and solve['remaining_degrees_of_freedom'] == 0, solve
    side = dict(operation='extrude', length=extrusion, extrusion_extent='length',
                angle_degrees=90, rotation_extent='angle', draft_angle_degrees=0, targets=[])
    feature = native.run('feature.create', sketch=sk, parameters=dict(sides=[side, {**side, 'operation':'none'}]))['container']
    chamfer=None
    if row['edge_chamfer']:
        edges=native.run('reference.list',kind='edge',owner=feature)['items']
        routes=[]
        for end in ('start:','end:'):
            rim=[dict(owner=feature,key=e['key']) for e in edges if e['key'].startswith(end)
                 and any(e['key'].endswith(f':end:{len(curve_id)}:{curve_id}') for curve_id in (first,last,top,bottom))]
            assert len(rim)==4, rim
            routes.append(dict(edges=rim))
        chamfer=native.run('chamfer.create',mode='equal_distance',calculation_method='occt',
                           distance_a_mm=row['edge_chamfer'],routes=routes)['container']
        c=row['edge_chamfer'];perimeter=2*(length-b)+math.pi*b
        expected_volume-=perimeter*c*c-2*math.pi*c*c*c/3
    holes = author_retaining_holes(native, row)
    expected_volume -= retaining_volume(row)
    seat = None
    if not row['woodruff']:
        seat = seating_face(native, feature)
        native.run('appearance.set', groups=[dict(id='key-seating-face', name='Z−',
                   faces=[seat], style=dict(color=SEATING_COLOR))])
        verify_seating_color(native, feature)
    native.run('document.material.set', properties=[dict(key='MASS_DENSITY', value='0.0000078', unit='kg/mm^3',
        descriptions=dict(cs='Hustota', en='Mass density', de='Dichte', fr='Masse volumique', ru='Плотность'))])
    names = dict(cs='Pero úsečové' if row['woodruff'] else 'Pero těsné',
                 en='Woodruff key' if row['woodruff'] else 'Parallel key',
                 de='Scheibenfeder' if row['woodruff'] else 'Passfeder',
                 fr='Clavette disque' if row['woodruff'] else 'Clavette parallèle',
                 ru='Сегментная шпонка' if row['woodruff'] else 'Призматическая шпонка')
    params = native.run('document.parameters.get')['parameters']
    for parameter in params:
        if parameter['key'] == 'name': parameter['values'] = names
        elif parameter['key'] == 'standard': parameter['values'] = {'': row['standard']}
        elif parameter['key'] == 'material': parameter['values'] = {'': 'C45 (1.0503)'}
        elif parameter['key'] == 'quantity': parameter['values'] = {'': '-'}
        elif parameter['key'] == 'mass': parameter['labels'] = dict(cs='Hmotnost [kg]',en='Mass [kg]',de='Masse [kg]',fr='Masse [kg]',ru='Масса [kg]')
    native.run('document.parameters.set', parameters=params)
    native.run('document.settings.set', precision=dict(decimal_places=6))
    native.run('save_as', path=str(path))
    # Save As authors an independent copy. Subsequent Relations belong to that
    # copy, not to the unnamed source retained by the shared command host.
    native.run('close',discard=True);native.run('open',path=str(path))
    # Read native dimension identities only; authoring always uses native APIs.
    saved = configparser.RawConfigParser(); saved.read(path, encoding='utf-8')
    entries = json.loads(saved['Document']['dimension_identifiers'])['entries']
    def number(owner, key):
        return 'd'+str(next(v['number'] for v in entries if v['owner']==owner and v['key']==key))
    a, c = number(sk, 'dimension:'+width), number(sk, 'dimension:'+extent)
    depth = number(feature, 'parameter:side0_length')
    stock = f'{depth} & "x" & ({c} + {a}/2) & "x" & {a}' if row['woodruff'] else f'{a} & "x" & {depth} & "x" & {c}'
    relations=f'stock = {stock}\nmass = model.mass\n'
    for hole in holes:
        positions=[f'({c} - {a}) / 2'] if row['retaining_count']==1 else [f'{a} / 2', f'{c} - 1.5 * {a}']
        for position, expression in zip(hole['positions'],positions):
            relations+=f'{number(hole["sketch"], "dimension:"+position)} = {expression}\n'
        if hole['counterbore']:
            relations+=f'{number(hole["feature"], "parameter:profile_offset")} = {depth} - {number(hole["feature"], "parameter:side0_length")}\n'
        else:
            relations+=f'{number(hole["feature"], "parameter:side0_length")} = {depth}\n'
    native.run('document.relations.set', relations=relations)
    native.run('regenerate'); assert not native.run('history.list')['calculation_errors']
    native.run('save'); native.run('close'); native.run('open', path=str(path))
    native.run('regenerate'); assert not native.run('history.list')['calculation_errors']
    parameters = native.run('document.parameters.get')['parameters']
    mass = next(v for v in parameters if v['key']=='mass')
    physical=native.run('document.relations.get')
    assert abs(physical['model_values']['model.volume']-expected_volume) <= 1e-5, physical
    assert abs(physical['model_values']['model.mass']-expected_volume*7800e-9) <= 1e-10, physical
    assert physical['parameters']['stock']==f'{b:g}x{h:g}x{length:g}', physical
    assert abs(float(mass['values'][''])-expected_volume*7800e-9) <= 0.00000051, mass
    if seat is not None:
        assert verify_seating_color(native, feature) == seat
    native.run('save'); native.run('close')
    return dict(sketch=sk, feature=feature, chamfer=chamfer, width_dimension=width, length_dimension=extent,
                expected_volume_mm3=expected_volume, expected_mass_kg=expected_volume*7800e-9,
                published_mass=mass, seating_face=seat, holes=holes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--cli', type=Path, default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    parser.add_argument('--filename')
    parser.add_argument('--retaining-only', action='store_true')
    args = parser.parse_args(); stage = args.stage.resolve(); stage.mkdir(parents=True, exist_ok=True)
    rows = variants(args.retaining_only)
    (stage/'manifest.json').write_text(json.dumps(rows, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    for row in rows:
        if args.filename and row['filename'] != args.filename: continue
        target = stage/row['filename']
        if target.exists(): raise RuntimeError('Refusing to overwrite '+str(target))
        receipt = stage/'evidence'/target.stem
        native = api.Native(args.cli.resolve(), receipt, settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try: result = author(native, row, target)
        finally: native.close()
        (receipt/'identities.json').write_text(json.dumps(result, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
        print('Authored and reopened '+target.name, flush=True)


if __name__ == '__main__':
    main()
