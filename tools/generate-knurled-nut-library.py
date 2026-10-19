"""Stage native historical knurled nuts with one circular Pattern of a cut.

Do not install before geometry, independent properties, repeated-edit,
reference, persistence, interaction and artwork gates have passed. The selected
CAD straight-knurl groove has a 90-degree profile and depth t/4. Its integer
tooth count is rounded from pi*D/t, and remains related to diameter and pitch.
This representation does not establish an ISO equivalent of the CSN nut.
"""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('nuts',ROOT/'tools/generate-hex-nut-library.py')
nuts=importlib.util.module_from_spec(spec);spec.loader.exec_module(nuts)
caps=nuts.caps
spec=importlib.util.spec_from_file_location('independent_knurl',ROOT/'tools/knurled-nut-volume.py')
independent=importlib.util.module_from_spec(spec);spec.loader.exec_module(independent)


def variants():
    table=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))['05-NUTS/08-KNURLED']['tables'][0]
    number=lambda x:float(x.strip('() ').replace(',','.'))
    result=[]
    for column,text in enumerate(table[0][1:],1):
        d=number(text.lstrip('M'))
        for low,standard in ((False,'021461'),(True,'021462')):
            names=('Nízká rýhovaná matice','Thin knurled nut','Niedrige Rändelmutter','Écrou moleté bas','Низкая рифлёная гайка') if low else (
                'Rýhovaná matice','Knurled nut','Rändelmutter','Écrou moleté','Рифлёная гайка')
            result.append(dict(filename=f'KN-CSN{standard}-M{d:04.1f}.prtz',standard='ČSN 02 '+standard[2:],family='08-KNURLED',
                diameter_mm=d,pitch_mm=None,outside_mm=number(table[4][column]),hub_diameter_mm=number(table[5][column]),
                head_height_mm=number(table[6][column]),height_mm=number(table[8 if low else 7][column]),
                chamfer_mm=number(table[1][column]),root_radius_mm=number(table[2][column]),knurl_pitch_mm=number(table[3][column]),
                clearance_depth_mm=0 if low else number(table[9][column]),clearance_diameter_mm=None if low else number(table[10][column]),
                low=low,names=dict(zip(caps.LANGUAGES,names)),density_kg_mm3=7.85e-6,material='Steel',
                review_status='candidate; historical dimensions; selected CAD straight-knurl depth t/4'))
    return result


def circle_feature(native,name,diameter,length,offset=0,subtract=False):
    sk=native.run('sketch.create',name=name,plane='XY')['sketch']
    circle=native.run('sketch.circle.create',sketch=sk,center=[0,0],radius_mm=diameter/2)['geometry']
    center=native.run('sketch.entity.get',sketch=sk,entity=circle)['entity']['center']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[center,'sketch_origin'])
    size=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle],value=diameter)['dimension']
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    feature=native.run('feature.create',sketch=sk,combine='subtract' if subtract else 'add',parameters=dict(
        symmetric=False,profile_plane_offset=offset,sides=[caps.side(length),caps.side(operation='none')]))['container']
    return dict(sketch=sk,size=size,feature=feature)


def circular_edge(native,z,radius,owner=None):
    candidates=[]
    offset=0
    while True:
        query=dict(kind='edge',offset=offset,limit=5000)
        if owner is not None:query['owner']=owner
        page=native.run('reference.list',**query)
        for ref in page['items']:
            data=native.run('reference.get',kind='edge',**caps.studs.ref(ref),limit=64)
            points=[p for segment in data['segments'] for p in segment['points']]
            if len(points)>3 and all(abs(p[2]-z)<1e-6 and abs(math.hypot(*p[:2])-radius)<1e-6 for p in points):candidates.append(ref)
        if not page['more']:break
        offset=page['next_offset']
    assert len(candidates)==1,('Current circular edge missing or ambiguous',z,radius,candidates)
    return caps.studs.ref(candidates[0])


def groove(native,v):
    depth=v['knurl_pitch_mm']/4;tip=v['outside_mm']/2-depth;outer=tip+2*depth
    sk=native.run('sketch.create',name='Straight knurl groove',plane='XY')['sketch']
    vertices=[[tip,0],[outer,2*depth],[outer,-2*depth]]
    curves=[native.run('sketch.segment.create',sketch=sk,first=vertices[i],second=vertices[(i+1)%3],snap_mm=1e-8)['geometry'] for i in range(3)]
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    a,b=entity(curves[0])['first'],entity(curves[0])['second'];c=entity(curves[1])['second']
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[a],geometry=['sketch_axis:x'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[curves[1]])
    dimensions={}
    for name,point,kind,axis,value in [('tip',a,'distance_x','y',tip),('outer',b,'distance_x','y',outer),
        ('upper',b,'distance_y','x',2*depth),('lower',c,'distance_y','x',-2*depth)]:
        dimensions[name]=native.run('sketch.dimension.create',sketch=sk,kind=kind,points=[point],geometry=['sketch_axis:'+axis],value=value)['dimension']
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    cut=native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(symmetric=False,
        profile_plane_offset=v['height_mm']-v['head_height_mm'],sides=[caps.side(v['head_height_mm']),caps.side(operation='none')]))['container']
    teeth=round(math.pi*v['outside_mm']/v['knurl_pitch_mm'])
    pattern=native.run('pattern.create',source=cut,mode='circular',local_axis='z',count=teeth,full_circle=True)['object']
    return dict(sketch=sk,feature=cut,pattern=pattern,**dimensions)


def revolved_blank(native,v):
    # Author the shoulder radius in the meridian. No edge created only by a
    # later Boolean intersection is needed as a Fillet support.
    h,k,r,R,c=v['height_mm'],v['head_height_mm'],v['hub_diameter_mm']/2,v['root_radius_mm'],v['chamfer_mm']
    outside=v['outside_mm']/2;level=h-k
    assert 0<R<=level and r+R<outside and c<k
    sk=native.run('sketch.create',name='Knurled nut meridian',plane='XZ')['sketch']
    p=[[0,0],[r,0],[r,-level+R],[r+R,-level],[outside,-level],[outside,-h+c],[outside-c,-h],[0,-h]]
    lines={}
    for i in (0,1,3,4,5,6,7):
        if i==1 and v['low']:continue
        lines[i]=native.run('sketch.segment.create',sketch=sk,first=p[i],second=p[(i+1)%8],snap_mm=1e-8)['geometry']
    arc=native.run('sketch.arc.create',sketch=sk,center=[r+R,-level+R],start=p[2],end=p[3],clockwise=False,snap_mm=1e-8)['geometry']
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    points={i:entity(lines[i])['first'] for i in lines}
    points[1]=entity(lines[0])['second']
    points[2]=points[1] if v['low'] else entity(lines[1])['second']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[points[0],'sketch_origin'])
    for i in (0,3,6):native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[lines[i]])
    for i in (1,4,7):
        if i in lines:native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[lines[i]])
    if v['low']:
        assert abs(level-R)<1e-8
        tangent=native.run('sketch.segment.create',sketch=sk,first=p[1],second=[r,1],construction=True,snap_mm=1e-8)['geometry']
        native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[tangent])
        native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[tangent],value=1)
    else:tangent=lines[1]
    for line in (tangent,lines[3]):native.run('sketch.constraint.create',sketch=sk,kind='tangent',geometry=[line,arc])
    dimensions={}
    for key,kind,a,b,value in [('hub','distance_x',0,1,r),('outside','distance_x',0,4,outside),
        ('height','distance_y',7,0,h),('head_height','distance_y',7,4,k),
        ('chamfer','distance_x',6,5,c),('chamfer_y','distance_y',7,5,c)]:
        dimensions[key]=native.run('sketch.dimension.create',sketch=sk,kind=kind,points=[points[a],points[b]],value=value,
            driving=not(v['low'] and key=='head_height'))['dimension']
    dimensions['radius']=native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[arc],value=R)['dimension']
    solved=native.run('sketch.solve_status',sketch=sk);assert solved['remaining_degrees_of_freedom']==0 and solved['maximum_residual']<1e-8,solved
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,0],second=[0,1],construction=True)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[axis],value=1)
    rotation=caps.side(operation='revolution');rotation.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,parameters=dict(axis_segment_id=axis,symmetric=False,
        sides=[rotation,caps.side(operation='none')]))['container']
    return dict(sketch=sk,feature=feature,**dimensions)


def author(native,v,target):
    thread=next(t for t in native.run('thread.catalog',standard='metric',limit=1000)['items']
        if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and 'x' not in t['designation'])
    v.update(pitch_mm=thread['pitch_mm'],bore_mm=thread['internal_root_diameter_mm'])
    native.run('new',type='part',name=target.stem)
    ids=dict(blank=revolved_blank(native,v))
    mouth=(v['diameter_mm']-v['bore_mm'])/2;v['mouth_chamfer_mm']=mouth
    ids['thread']=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='through_all',thread_end='length',
        bore_length_mm=v['height_mm'],thread_length_mm=v['height_mm'],runout_pitch_factor=0,
        chamfer_enabled=True,chamfer_depth_mm=mouth,chamfer_angle_degrees=90,drill_point_enabled=False,placement=dict(z=0))['container']
    native.run('regenerate')
    errors=native.run('history.list')['calculation_errors'];assert not errors,errors
    upper=circular_edge(native,v['height_mm'],v['bore_mm']/2,owner=ids['thread'])
    ids['mouth']=native.run('chamfer.create',mode='equal_distance',distance_a_mm=mouth,routes=[dict(edges=[upper])])['container']
    if v['clearance_depth_mm']:
        ids['clearance']=circle_feature(native,'Unthreaded hub clearance',v['clearance_diameter_mm'],v['clearance_depth_mm'],subtract=True)
    # Finish simple axial features before creating the many knurl faces. This
    # keeps edge selection narrow and avoids rebuilding the Pattern per hole.
    ids['groove']=groove(native,v)
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',
        descriptions=dict(zip(caps.LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    parameters=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},stock={'':thread['designation']},material={'':v['material']},quantity={'':'-'})
    for p in parameters:
        if p['key'] in values:p['values']=values[p['key']]
    native.run('document.parameters.set',parameters=parameters);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8')
    entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    def symbol(owner,key):return 'd'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    def dim(item,key='size'):return symbol(item['sketch'],'dimension:'+item[key])
    blank=ids['blank'];h=dim(blank,'height')
    # The thin execution has no straight hub wall: its inspected head height
    # is h-R. Relations consume its authored drivers, never a measured result.
    k=f'({h} - {dim(blank,"radius")})' if v['low'] else dim(blank,'head_height')
    radius=dim(blank,'outside');g=ids['groove'];t=dim(g,'upper')
    lines=['mass = model.mass',f'stock = "M" & {symbol(ids["thread"],"parameter:thread_designation")} & "x" & {symbol(ids["thread"],"parameter:pitch")}',
        f'{dim(blank,"chamfer_y")} = {dim(blank,"chamfer")}',f'{symbol(g["feature"],"parameter:profile_offset")} = {h} - {k}',
        f'{symbol(g["feature"],"parameter:side0_length")} = {k}',f'{dim(g,"tip")} = {radius} - {t} / 2',
        f'{dim(g,"outer")} = {radius} + {t} / 2',f'{dim(g,"lower")} = -{t}',
        f'{symbol(g["pattern"],"parameter:pattern:count")} = round(pi * {radius} / {t})',
        f'{symbol(ids["thread"],"parameter:thread_length")} = {h}',f'{symbol(ids["thread"],"parameter:bore_length")} = {h}']
    native.run('document.relations.set',relations='\n'.join(lines)+'\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close');native.run('open',path=str(target));native.run('close')
    return ids


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--filename');args=p.parse_args()
    stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);completed=[]
    for v in variants():
        if args.filename and args.filename!=v['filename']:continue
        target=stage/v['filename'];assert not target.exists();evidence=stage/'evidence'/target.stem
        native=caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=author(native,v,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        v['independent_volume_mm3']=independent.volume(v)
        completed.append(v);(stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)


if __name__=='__main__':main()
