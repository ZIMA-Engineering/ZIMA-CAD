"""Author cylindrical-head screw candidates through current native commands.

ISO 4762 uses a centered cylinder head, rounded outer rim and an offset-plane
hexagonal Through All cut towards the head face. Historical dimension sources
remain distinguishable from independently checked standard requirements.
"""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('studs',ROOT/'tools/generate-stud-library.py')
studs=importlib.util.module_from_spec(spec);spec.loader.exec_module(studs)
spec=importlib.util.spec_from_file_location('socket_profile',ROOT/'tools/socket-entrance-profile.py')
socket_profile=importlib.util.module_from_spec(spec);spec.loader.exec_module(socket_profile)
LANGUAGES=studs.LANGUAGES


def variants():
    source=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))
    reviewed=json.loads((ROOT/'tools/data/cap-screw-reviewed-dimensions.json').read_text(encoding='utf-8'))
    result=[];number=lambda v:float(v.strip('()').replace(',','.'))
    families=[('ISO 4762','01-SOCKET','01-M1.6-M12'),('ISO 4762','01-SOCKET','02-M14-M64'),
        ('ISO 1207','02-SLOTTED',''),('ISO 1580','03-LARGE','')]
    for standard,family,group in families:
        key='04-SCREWS/04-CAP-HEAD/'+family+('/'+group if group else '')
        dims,lengths=source[key]['tables'][:2]
        socket=family=='01-SOCKET'
        for col,title in enumerate(dims[0][1:],1):
            d=number(title.strip('()')[1:]);pitch=number(dims[1][col])
            dk=number(dims[3 if socket else 4][col]);k=number(dims[11 if socket else 7][col]);r=number(dims[13 if socket else 12][col])
            if standard in reviewed:
                values=reviewed[standard];k=values['head_heights_mm'][values['diameters_mm'].index(d)]
            available=[]
            for row in lengths[1 if socket else 2:]:
                try:l=number(row[0])
                except ValueError:continue
                value=row[col+(2 if socket else 1)].strip()
                if value and ('+' in value or value.replace(',','').replace('.','').isdigit()):available.append(l)
            assert available,(standard,d)
            desired=min(max(4*d,20),max(available));length=next(l for l in available if l>=desired)
            thread_ref=number(dims[2 if socket else 3][col])
            smooth=3*pitch if socket else number(dims[2][col])
            selected=next(row[col+(2 if socket else 1)] for row in lengths if row[0].strip('()').replace(',','.').replace('.','',1).isdigit() and number(row[0])==length)
            full_thread=socket and '+' in selected
            thread=length-smooth-2*pitch if full_thread else min(thread_ref,length-smooth-2*pitch)
            assert thread>pitch and length-thread-2*pitch>=r
            # A nominal key must fit; use the specified minimum across-flats
            # dimension rather than a geometrically undersized nominal hexagon.
            drive=(number(dims[14][col]) if dims[17][col]=='–' else number(dims[17][col])) if socket else number(dims[11][col])
            depth=number(dims[18 if socket else (14 if standard=='ISO 1580' else 13)][col])
            entry=drive/2+.1*pitch if socket else None
            engagement=entry-drive/2 if socket else 0
            depth+=engagement
            floor_min=number(dims[21][col]) if socket else 0
            assert k-depth>=floor_min-1e-8,(standard,d,'socket floor wall')
            v=number(dims[19][col]) if socket else min(.1*d,k/4)
            edge_radius=min(v/2,k/5)
            if standard=='ISO 1580':edge_radius=number(dims[13][col])
            names=dict(zip(LANGUAGES,(
                'Šroub s válcovou hlavou '+('s vnitřním šestihranem' if socket else 's drážkou'),
                'Hexagon socket head cap screw' if socket else 'Slotted '+('pan' if standard=='ISO 1580' else 'cheese')+' head screw',
                'Zylinderschraube mit Innensechskant' if socket else 'Schlitzschraube mit '+('Flachkopf' if standard=='ISO 1580' else 'Zylinderkopf'),
                'Vis à tête cylindrique à six pans creux' if socket else 'Vis à tête '+('cylindrique large' if standard=='ISO 1580' else 'cylindrique')+' fendue',
                'Винт с цилиндрической головкой и '+('внутренним шестигранником' if socket else 'прямым шлицем'))))
            size=f'{d:04.1f}'
            result.append(dict(filename=f'{"SHCS" if socket else "SCHS"}-'+standard.replace(' ','')+f'-M{size}x{int(length):03d}.prtz',
                family=family,standard=standard,designation=('M'+f'{d:g}').replace('.',','),diameter_mm=d,pitch_mm=pitch,
                head_diameter_mm=dk,head_height_mm=k,underhead_radius_mm=r,head_edge_radius_mm=edge_radius,
                drive_width_mm=drive,drive_depth_mm=depth,socket=socket,length_mm=length,thread_length_mm=thread,
                full_thread=full_thread,
                socket_entry_radius_mm=drive/math.sqrt(3) if socket else None,
                socket_entry_kind='geometry_corner' if socket else None,
                socket_effective_depth_mm=depth-(drive/math.sqrt(3)-drive/2) if socket else depth,
                head_floor_min_mm=floor_min,
                property_class='8.8' if socket else '4.8',names=names,preferred=not title.startswith('(')))
    return result


def side(length=1,operation='extrude',extent='length'):
    return dict(operation=operation,length=length,extrusion_extent=extent,angle_degrees=90,
        rotation_extent='angle',draft_angle_degrees=0,targets=[])


def disk(native,diameter,length,negative=False):
    sk=native.run('sketch.create',name='Profile',plane='XY')['sketch']
    circle=native.run('sketch.circle.create',sketch=sk,center=[0,0],radius_mm=diameter/2)['geometry']
    center=native.run('sketch.entity.get',sketch=sk,entity=circle)['entity']['center']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[center,'sketch_origin'])
    native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle],value=diameter)
    sides=[side(length),side(operation='none')]
    if negative:sides.reverse()
    return native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,sides=sides))['container']


def circular_edge(native,owner,z,radius):
    candidates=[]
    for r in native.run('reference.list',kind='edge',owner=owner)['items']:
        g=native.run('reference.get',kind='edge',**studs.ref(r),limit=2)
        if not g['segments']:continue
        point=g['segments'][0]['points'][0]
        if abs(point[2]-z)<1e-6 and abs(math.hypot(*point[:2])-radius)<1e-6:candidates.append(r)
    if len(candidates)>1:
        feature=native.run('feature.get',container=owner)
        active='end' if feature['parameters']['sides'][0]['operation']!='none' else 'start'
        candidates=[r for r in candidates if ':'+feature['feature']+':'+active+':' in r['key']]
    assert len(candidates)==1,(owner,z,radius,candidates)
    return studs.ref(candidates[0])


def drive(native,v):
    sk=native.run('sketch.create',name='Drive profile',plane='XY')['sketch']
    if v['socket']:
        native.run('sketch.polygon.create',sketch=sk,center=[0,0],rim=[v['drive_width_mm']/math.sqrt(3),0],sides=6)
        circles=[r for r in native.run('sketch.entities',sketch=sk)['items'] if r['kind']=='circle']
        assert len(circles)==1,circles
        rim=native.run('sketch.entity.get',sketch=sk,entity=circles[0]['id'])['entity']
        native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[rim['center'],'sketch_origin'])
        native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[rim['id']],value=2*v['drive_width_mm']/math.sqrt(3))
        segments=[r for r in native.run('sketch.entities',sketch=sk)['items'] if r['kind']=='segment']
        points=[]
        for segment in segments:
            e=native.run('sketch.entity.get',sketch=sk,entity=segment['id'])['entity']
            for key in ('first','second'):
                if key in e:points.append(e[key])
        # The construction circle fixes polygon size; a vertex on X fixes rotation.
        for p in dict.fromkeys(points):
            point=native.run('sketch.entity.get',sketch=sk,entity=p)['entity']
            if abs(point['y'])<1e-8 and point['x']>0:
                native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[p],geometry=['sketch_axis:x']);break
    else:
        curves=native.run('sketch.rectangle.create',sketch=sk,first=[-v['head_diameter_mm'], -v['drive_width_mm']/2],
            second=[v['head_diameter_mm'],v['drive_width_mm']/2])['geometry']
        # Author the open-ended slot with dimensions to the Sketch axes.
        seen=set()
        for curve in curves:
            e=native.run('sketch.entity.get',sketch=sk,entity=curve)['entity']
            for key in ('first','second'):
                if key in e:seen.add(e[key])
        for p in seen:
            e=native.run('sketch.entity.get',sketch=sk,entity=p)['entity']
            if e['x']<0 and e['y']<0:
                native.run('sketch.dimension.create',sketch=sk,kind='distance_x',points=[p],geometry=['sketch_axis:y'],value=e['x'])
                native.run('sketch.dimension.create',sketch=sk,kind='distance_y',points=[p],geometry=['sketch_axis:x'],value=e['y']);break
        native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[curves[0]],value=2*v['head_diameter_mm'])
        native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[curves[1]],value=v['drive_width_mm'])
    return native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(symmetric=False,
        profile_plane_offset=-(v['head_height_mm']-v['drive_depth_mm']),
        sides=[side(operation='none'),side(v['head_height_mm'],extent='through_all')]))['container']


def relations(native,path,shaft):
    data=configparser.RawConfigParser();data.read(path,encoding='utf-8')
    entries=json.loads(data['Document']['dimension_identifiers'])['entries']
    sketch=native.run('feature.get',container=shaft)['sketch']
    diameter=next(e['number'] for e in entries if e['owner']==sketch and e['key'].startswith('dimension:'))
    length=next(e['number'] for e in entries if e['owner']==shaft and e['key']=='parameter:side0_length')
    native.run('document.relations.set',relations=f'stock = "M" & d{diameter} & "x" & d{length}\nmass = model.mass\n')


def socket_entry(native,v,opening,existing=None):
    return socket_profile.build(native,v,opening,side,existing)


def author(native,v,path,head_builder=None,drive_builder=None):
    native.run('new',type='part',name=path.stem)
    shaft=disk(native,v['diameter_mm'],v['length_mm'])
    if head_builder:
        head=head_builder(native,v);rounded=head
    else:
        head=disk(native,v['head_diameter_mm'],v['head_height_mm'],True)
        top=circular_edge(native,head,-v['head_height_mm'],v['head_diameter_mm']/2)
        rounded=native.run('fillet.create',radius_mm=v['head_edge_radius_mm'],routes=[dict(edges=[top])])['container']
    under=circular_edge(native,shaft,0,v['diameter_mm']/2)
    fillet=native.run('fillet.create',radius_mm=v['underhead_radius_mm'],routes=[dict(edges=[under])])['container']
    tip=circular_edge(native,shaft,v['length_mm'],v['diameter_mm']/2)
    chamfer=native.run('chamfer.create',mode='equal_distance',distance_a_mm=.5*v['pitch_mm'],routes=[dict(edges=[tip])])['container']
    faces=studs.faces(native,shaft)
    cylinder=next(r for r,s in faces if s and s['kind']=='cylinder')
    start=next(r for r,s in faces if s and s['kind']=='plane' and abs(s['origin'][2]-v['length_mm'])<1e-6)
    cone=next(r for r,s in studs.faces(native,chamfer) if s and s['kind']=='cone')
    thread=native.run('shaft_thread.create',cylinder=studs.ref(cylinder),start=studs.ref(start),chamfer=studs.ref(cone),
        designation=v['designation'],length_mm=v['thread_length_mm'],end_condition='length',runout_enabled=True,runout_pitch_factor=2)['container']
    opening=(drive_builder or drive)(native,v)
    entry=socket_entry(native,v,opening) if v['socket'] and not v.get('socket_no') else None
    native.run('document.material.load',path=str(ROOT/v.get('material_file',f'config/materials/01_steels/fasteners/Steel-{v["property_class"]}.matz')))
    params=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},stock={'':v['designation'].replace(',','.')+f'x{v["length_mm"]:g}'},
        material={'':v['property_class']},quantity={'':'-'})
    for p in params:
        if p['key'] in values:p['values']=values[p['key']]
    native.run('document.parameters.set',parameters=params)
    native.run('document.relations.set',relations='mass = model.mass\n')
    native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    native.run('save_as',path=str(path));native.run('close',discard=True);native.run('open',path=str(path))
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    relations(native,path,shaft)
    native.run('save');native.run('close')
    return dict(shaft=shaft,head=head,rounded=rounded,fillet=fillet,chamfer=chamfer,thread=thread,opening=opening,entry=entry)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--filename')
    p.add_argument('--refresh-relations',action='store_true')
    p.add_argument('--refresh-thread-limits',action='store_true')
    p.add_argument('--standard')
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe');args=p.parse_args()
    out=args.stage.resolve();out.mkdir(parents=True,exist_ok=True);rows=variants()
    (out/'manifest.json').write_text(json.dumps(dict(variants=rows),ensure_ascii=False,indent=2),encoding='utf-8')
    for v in rows:
        if args.standard and args.standard!=v['standard']:continue
        if args.filename and args.filename!=v['filename']:continue
        path=out/v['filename']
        if path.exists():
            assert (out/'evidence'/path.stem/'identities.json').is_file(),path
            if args.refresh_relations or args.refresh_thread_limits:
                identity=json.loads((out/'evidence'/path.stem/'identities.json').read_text(encoding='utf-8'))
                native=studs.api.Native(args.cli.resolve(),out/'relation-evidence'/path.stem)
                try:
                    native.run('open',path=str(path))
                    if args.refresh_thread_limits:native.run('shaft_thread.set',container=identity['thread'],length_mm=v['thread_length_mm'])
                    relations(native,path,identity['shaft']);native.run('regenerate');native.run('save');native.run('close')
                finally:native.close()
            continue
        native=studs.api.Native(args.cli.resolve(),out/'evidence'/path.stem,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:identity=author(native,v,path)
        finally:native.close()
        (out/'evidence'/path.stem/'identities.json').write_text(json.dumps(identity,indent=2),encoding='utf-8')
        print('Authored and reopened '+path.name,flush=True)


if __name__=='__main__':main()
