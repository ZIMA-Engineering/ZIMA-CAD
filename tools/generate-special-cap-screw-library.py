"""Author reviewed DIN low-head and ISO hexalobular native library candidates."""
import argparse
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('caps',ROOT/'tools/generate-cap-screw-library.py')
caps=importlib.util.module_from_spec(spec);spec.loader.exec_module(caps)
DATA=ROOT/'tools/data/special-cap-screw-reviewed-dimensions.json'


def variants():
    result=[]
    for family in json.loads(DATA.read_text(encoding='utf-8'))['families']:
        for row in family['sizes']:
            d=row['d'];pitch=row['P'];length=row['default_l'];radius=row['r']
            full_catalogue=family['standard']=='ISO 14579'
            thread=length-radius-2*pitch if full_catalogue else min(row['b'],length-radius-2*pitch)
            result.append(dict(filename=family['prefix']+'-'+family['standard'].replace(' ','')+f'-M{d:04.1f}x{length:03d}.prtz',
                family=family['directory'],standard=family['standard'],designation=f'M{d:g}'.replace('.',','),diameter_mm=d,pitch_mm=pitch,
                head_diameter_mm=row['dk'],head_height_mm=row['k'],underhead_radius_mm=radius,
                head_edge_radius_mm=min(.1*pitch,row['k']/10),drive_width_mm=row.get('s',row.get('A')),
                drive_depth_mm=row['t'],socket=True,length_mm=length,thread_length_mm=thread,
                full_thread=full_catalogue or thread<row['b'],property_class='08.8' if family['standard'].startswith('DIN') else '8.8',
                material_file='config/materials/01_steels/fasteners/Steel-8.8.matz',
                names=dict(zip(caps.LANGUAGES,family['names'])),preferred=True,
                socket_entry_kind=None if full_catalogue else 'geometry_corner',
                socket_entry_radius_mm=None if full_catalogue else row['s']/math.sqrt(3),
                **{key:row[key] for key in ('A','B','socket_no','dh','t2') if key in row}))
    return result


def hexalobular_arcs(A,B):
    """Nominal CAD contour: Annex A Re≈0.1A; exact A/B and C1 circle joins.

    Ri is solved from external tangency instead of rounding the approximate
    Annex A 0.175A relationship and leaving gaps between the twelve arcs.
    This representation does not certify manufacturing GO/NO GO conformance.
    """
    re=.1*A;c=A/2-re;b=B/2;cs=math.cos(math.pi/6)
    ri=(re*re-c*c-b*b+2*c*b*cs)/(2*b-2*c*cs-2*re)
    assert ri>0
    centers=[];radii=[]
    for j in range(12):
        angle=math.pi/2+j*math.pi/6
        radius=c if j%2==0 else b+ri
        centers.append([radius*math.cos(angle),radius*math.sin(angle)])
        radii.append(re if j%2==0 else ri)
    junctions=[]
    for j in range(12):
        a,z=centers[j],centers[(j+1)%12];r,s=radii[j],radii[(j+1)%12]
        assert abs(math.dist(a,z)-r-s)<1e-9
        junctions.append([(s*a[k]+r*z[k])/(r+s) for k in (0,1)])
    return [dict(center=centers[j],start=junctions[(j-1)%12],end=junctions[j],clockwise=bool(j%2),radius=radii[j]) for j in range(12)]


def coordinate(native,sk,point,xy):
    for axis,value in zip(('x','y'),xy):
        if abs(value)<1e-10:
            native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[point],geometry=['sketch_axis:'+('y' if axis=='x' else 'x')])
        else:native.run('sketch.dimension.create',sketch=sk,kind='distance_'+axis,points=[point],geometry=['sketch_axis:'+('y' if axis=='x' else 'x')],value=value)


def torx_drive(native,v):
    sk=native.run('sketch.create',name='Hexalobular profile',plane='XY')['sketch']
    arcs=hexalobular_arcs(v['A'],v['B']);geometry=[]
    for arc in arcs:
        geometry.append(native.run('sketch.arc.create',sketch=sk,**{k:arc[k] for k in ('center','start','end','clockwise')},snap_mm=1e-7)['geometry'])
    points={}
    for g in geometry:
        entity=native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
        # Clockwise creation may reverse persisted start/end order. Capture
        # native junction identities and positions before constraining them.
        for key in ('start','end'):
            point=entity[key]
            if point not in points:
                p=native.run('sketch.entity.get',sketch=sk,entity=point)['entity'];points[point]=[p['x'],p['y']]
    assert len(points)==12,'The hexalobular boundary must share all twelve junctions'
    for point,xy in points.items():coordinate(native,sk,point,xy)
    for g,arc in zip(geometry,arcs):native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[g],value=arc['radius'])
    return native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(symmetric=False,
        profile_plane_offset=-v['head_height_mm']+v['drive_depth_mm'],
        sides=[caps.side(operation='none'),caps.side(v['head_height_mm'],extent='through_all')]))['container']


def guide(native,v):
    """DIN 6912 pilot cylinder and 120-degree transition below the hex socket."""
    floor=-v['head_height_mm']+v['drive_depth_mm'];bottom=-v['head_height_mm']+v['t2']
    outer=v['drive_width_mm']/2;inner=v['dh']/2
    transition=floor+(outer-inner)/math.tan(math.pi/3)
    assert transition<bottom<v['length_mm']
    xy=[[0,-floor],[outer,-floor],[inner,-transition],[inner,-bottom],[0,-bottom]]
    sk=native.run('sketch.create',name='Pilot recess profile',plane='XZ')['sketch'];seen=set()
    for index,first in enumerate(xy):
        g=native.run('sketch.segment.create',sketch=sk,first=first,second=xy[(index+1)%len(xy)])['geometry']
        e=native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
        for key,point in (('first',first),('second',xy[(index+1)%len(xy)])):
            if e[key] not in seen:coordinate(native,sk,e[key],point);seen.add(e[key])
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,0],second=[0,-v['head_height_mm']],construction=True)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    e=native.run('sketch.entity.get',sketch=sk,entity=axis)['entity']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[e['first'],'sketch_origin'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    native.run('sketch.dimension.create',sketch=sk,kind='distance_y',points=[e['second']],geometry=['sketch_axis:x'],value=-v['head_height_mm'])
    rotation=caps.side(operation='revolution');rotation.update(rotation_extent='full',angle_degrees=360)
    return native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(axis_segment_id=axis,symmetric=False,sides=[rotation,caps.side(operation='none')]))['container']


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--filename');parser.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe');args=parser.parse_args()
    out=args.stage.resolve();out.mkdir(parents=True,exist_ok=True);rows=variants()
    (out/'manifest.json').write_text(json.dumps(dict(variants=rows),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    for v in rows:
        if args.filename and args.filename!=v['filename']:continue
        target=out/v['filename']
        if target.exists():
            assert (out/'evidence'/target.stem/'identities.json').is_file(),target
            continue
        native=caps.studs.api.Native(args.cli.resolve(),out/'evidence'/target.stem,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:
            guide_identity={}
            builder=torx_drive if 'socket_no' in v else None
            if 'dh' in v:
                def builder(host,row):
                    opening=caps.drive(host,row)
                    guide_identity['guide']=guide(host,row)
                    return opening
            identity=caps.author(native,v,target,drive_builder=builder)
            identity.update(guide_identity)
        finally:native.close()
        (out/'evidence'/target.stem/'identities.json').write_text(json.dumps(identity,indent=2),encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)


if __name__=='__main__':main()
