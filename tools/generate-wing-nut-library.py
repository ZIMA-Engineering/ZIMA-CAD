"""Stage editable cast wing-nut studies; never install unfinished candidates.

The historical table identifies CSN 02 1665, not DIN 315. Initial studies
preserve the hub diameters, heights, span, taper and accepted root radii.
Perimeter rounding remains unresolved; these are not complete factory models.
"""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('wing_caps',ROOT/'tools/generate-cap-screw-library.py')
caps=importlib.util.module_from_spec(spec);spec.loader.exec_module(caps)


def variants():
    source=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))['05-NUTS/09-WING']
    table=source['tables'][0];number=lambda x:float(x.replace(',','.'))
    rows=[]
    for col in range(7,11):
        d=number(table[0][col][1:])
        rows.append(dict(filename=f'WN-CSN021665-M{d:04.1f}.prtz',diameter_mm=d,
            hub_top_diameter_mm=number(table[1][col]),hub_base_diameter_mm=number(table[2][col]),
            span_mm=number(table[3][col]),tip_thickness_mm=number(table[4][col]),
            root_thickness_mm=number(table[5][col]),height_mm=number(table[6][col]),
            hub_height_mm=number(table[7][col]),crown_radius_mm=number(table[8][col]),
            edge_radius_mm=number(table[9][col]),root_radius_mm=number(table[10][col]),
            secondary_edge_radius_mm=number(table[11][col]),indicative_source_mass_g=number(table[13][col]),
            source_sha256=source['sha256'],standard='ČSN 02 1665',family='09-WING',
            density_kg_mm3=7.85e-6,material='Steel',review_status='unreleased study; perimeter rounding not accepted; M16 front-radius source conflict remains open',
            names=dict(zip(caps.LANGUAGES,('Studie křídlové matice','Wing nut study','Flügelmutterstudie','Étude d’écrou à oreilles','Исследование барашковой гайки')))))
    return rows


def dimension(native,sketch,kind,points=None,geometry=None,value=None):
    arguments=dict(sketch=sketch,kind=kind,value=value)
    if points is not None:arguments['points']=points
    if geometry is not None:arguments['geometry']=geometry
    return native.run('sketch.dimension.create',**arguments)['dimension']


def polygon(native,sketch,vertices):
    return [native.run('sketch.segment.create',sketch=sketch,first=a,second=vertices[(i+1)%len(vertices)],snap_mm=1e-8)['geometry'] for i,a in enumerate(vertices)]


def hub(native,v):
    sketch=native.run('sketch.create',name='Tapered hub meridian',plane='XZ')['sketch']
    r,t,h=v['hub_base_diameter_mm']/2,v['hub_top_diameter_mm']/2,v['hub_height_mm']
    lines=polygon(native,sketch,[[0,0],[r,0],[t,-h],[0,-h]])
    entity=lambda key:native.run('sketch.entity.get',sketch=sketch,entity=key)['entity']
    points=[entity(line)['first'] for line in lines]
    native.run('sketch.constraint.create',sketch=sketch,kind='point_reference',points=[points[0],'sketch_origin'])
    for i in (0,2):native.run('sketch.constraint.create',sketch=sketch,kind='horizontal',geometry=[lines[i]])
    native.run('sketch.constraint.create',sketch=sketch,kind='vertical',geometry=[lines[3]])
    drivers={}
    for name,point,kind,axis,value in [('base_radius',points[1],'distance_x','y',r),('top_radius',points[2],'distance_x','y',t),('height',points[2],'distance_y','x',-h)]:
        drivers[name]=dimension(native,sketch,kind,points=[point],geometry=['sketch_axis:'+axis],value=value)
    axis=native.run('sketch.segment.create',sketch=sketch,first=[0,0],second=[0,1],construction=True,snap_mm=1e-8)['geometry']
    native.run('sketch.segment.centerline',sketch=sketch,segment=axis,centerline=True)
    native.run('sketch.constraint.create',sketch=sketch,kind='vertical',geometry=[axis])
    dimension(native,sketch,'distance',geometry=[axis],value=1)
    status=native.run('sketch.solve_status',sketch=sketch);assert status['remaining_degrees_of_freedom']==0,status
    rotation=caps.side(operation='revolution');rotation.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sketch,parameters=dict(axis_segment_id=axis,symmetric=False,sides=[rotation,caps.side(operation='none')]))['container']
    return dict(sketch=sketch,feature=feature,**drivers)


def wing(native,v):
    radius=v['crown_radius_mm'];cx=v['span_mm']/2-radius;cz=v['height_mm']-radius
    q=[v['hub_top_diameter_mm']/2,v['hub_height_mm']]
    dx,dz=q[0]-cx,q[1]-cz;squared=dx*dx+dz*dz
    assert squared>radius*radius
    along=radius*radius/squared;across=radius*math.sqrt(squared-radius*radius)/squared
    candidates=[[cx+along*dx-sign*across*dz,cz+along*dz+sign*across*dx] for sign in (-1,1)]
    t=min(candidates,key=lambda p:p[0])
    p=[cx+radius/2,cz-radius*math.sqrt(3)/2]
    a=[p[0]-p[1]*math.sqrt(3),0]
    assert 0<a[0]<v['hub_base_diameter_mm']/2,(a,v)
    local=lambda p:[p[0],-p[1]]
    sketch=native.run('sketch.create',name='Cast wing outline study',plane='XZ')['sketch']
    outer=native.run('sketch.segment.create',sketch=sketch,first=local(a),second=local(p))['geometry']
    arc=native.run('sketch.arc.create',sketch=sketch,center=[cx,-cz],start=local(p),end=local(t),clockwise=True,snap_mm=1e-8)['geometry']
    inner=native.run('sketch.segment.create',sketch=sketch,first=local(t),second=local(q),snap_mm=1e-8)['geometry']
    landing_x=q[0]/8
    landing=native.run('sketch.segment.create',sketch=sketch,first=local(q),second=[landing_x,-q[1]],snap_mm=1e-8)['geometry']
    native.run('sketch.segment.create',sketch=sketch,first=[landing_x,-q[1]],second=local(a),snap_mm=1e-8)
    entity=lambda key:native.run('sketch.entity.get',sketch=sketch,entity=key)['entity']
    centre=entity(arc)['center'];root=entity(inner)['second'];base=entity(outer)['first']
    drivers={}
    for name,point,kind,axis,value in [('root_x',root,'distance_x','y',q[0]),('root_y',root,'distance_y','x',-q[1])]:
        drivers[name]=dimension(native,sketch,kind,points=[point],geometry=['sketch_axis:'+axis],value=value)
    for name,position,kind,axis,value,alignment in [('height',[cx,-v['height_mm']],'distance_y','x',-v['height_mm'],'vertical'),
        ('half_span',[v['span_mm']/2,-cz],'distance_x','y',v['span_mm']/2,'horizontal')]:
        point=native.run('sketch.point.create',sketch=sketch,position=position,construction=True,snap_mm=1e-8)['point']
        native.run('sketch.constraint.create',sketch=sketch,kind='point_on_circle',points=[point],geometry=[arc])
        native.run('sketch.constraint.create',sketch=sketch,kind=alignment,points=[centre,point])
        drivers[name]=dimension(native,sketch,kind,points=[point],geometry=['sketch_axis:'+axis],value=value)
    native.run('sketch.constraint.create',sketch=sketch,kind='point_on_line',points=[base],geometry=['sketch_axis:x'])
    native.run('sketch.constraint.create',sketch=sketch,kind='horizontal',geometry=[landing])
    drivers['landing_x']=dimension(native,sketch,'distance_x',points=[entity(landing)['second']],geometry=['sketch_axis:y'],value=landing_x)
    drivers['radius']=dimension(native,sketch,'radius',geometry=[arc],value=radius)
    drivers['flank_angle']=dimension(native,sketch,'angle_between',geometry=['sketch_axis:x',outer],value=30)
    for line in (outer,inner):native.run('sketch.constraint.create',sketch=sketch,kind='tangent',geometry=[line,arc])
    root_round=''
    if v['secondary_edge_radius_mm']:
        root_round=native.run('sketch.corner_fillet.create',sketch=sketch,first=inner,second=landing,radius_mm=v['secondary_edge_radius_mm'])['geometry']
        drivers['root_radius']=dimension(native,sketch,'radius',geometry=[root_round],value=v['secondary_edge_radius_mm'])
    status=native.run('sketch.solve_status',sketch=sketch);assert status['remaining_degrees_of_freedom']==0,status
    feature=native.run('feature.create',sketch=sketch,combine='add',parameters=dict(symmetric=False,
        profile_plane_offset=-v['root_thickness_mm']/2,
        sides=[caps.side(v['root_thickness_mm']),caps.side(operation='none')]))['container']
    errors=native.run('history.list')['calculation_errors'];assert not errors,errors
    pattern=native.run('pattern.create',source=feature,mode='circular',local_axis='z',count=2,full_circle=True)['object']
    return dict(sketch=sketch,feature=feature,pattern=pattern,arc=arc,root_round=root_round,**drivers)


def taper(native,v):
    sketch=native.run('sketch.create',name='Wing thickness taper',plane='YZ')['sketch']
    a,b,h=v['root_thickness_mm']/2,v['tip_thickness_mm']/2,v['height_mm']
    end=h+1
    lines=polygon(native,sketch,[[a,0],[b+(b-a)/h,end],[2*a,end],[2*a,0]])
    entity=lambda key:native.run('sketch.entity.get',sketch=sketch,entity=key)['entity']
    points=[entity(line)['first'] for line in lines]
    for i in (1,3):native.run('sketch.constraint.create',sketch=sketch,kind='horizontal',geometry=[lines[i]])
    native.run('sketch.constraint.create',sketch=sketch,kind='vertical',geometry=[lines[2]])
    native.run('sketch.constraint.create',sketch=sketch,kind='point_on_line',points=[points[0]],geometry=['sketch_axis:x'])
    tip=native.run('sketch.point.create',sketch=sketch,position=[b,h],construction=True)['point']
    native.run('sketch.constraint.create',sketch=sketch,kind='point_on_line',points=[tip],geometry=[lines[0]])
    drivers={}
    for name,point,kind,axis,value in [('root',points[0],'distance_x','y',a),('tip',tip,'distance_x','y',b),
        ('height',tip,'distance_y','x',h),('end_height',points[1],'distance_y','x',end),('outside',points[2],'distance_x','y',2*a)]:
        drivers[name]=dimension(native,sketch,kind,points=[point],geometry=['sketch_axis:'+axis],value=value)
    status=native.run('sketch.solve_status',sketch=sketch);assert status['remaining_degrees_of_freedom']==0,status
    reach=v['span_mm']/2+1
    feature=native.run('feature.create',sketch=sketch,combine='subtract',parameters=dict(symmetric=False,profile_plane_offset=-reach,sides=[caps.side(2*reach),caps.side(operation='none')]))['container']
    pattern=native.run('pattern.create',source=feature,mode='circular',local_axis='z',count=2,full_circle=True)['object']
    return dict(sketch=sketch,feature=feature,pattern=pattern,**drivers)


def roundings(native,v,kind):
    def edges(kind):
        found=[];h,m=v['hub_height_mm'],v['height_mm'];cx=v['span_mm']/2-v['crown_radius_mm'];cz=m-v['crown_radius_mm']
        for edge in native.run('edge_treatment.edges',limit=5000)['items']:
            if edge['ambiguous']:continue
            points=[p['position_mm'] for segment in edge['segments'] for p in segment['endpoints']]
            if len(points)!=2 or points[0][0]*points[1][0]<=0 or points[0][1]*points[1][1]<=0:continue
            on_side=all(abs(abs(p[1])-(v['root_thickness_mm']+(v['tip_thickness_mm']-v['root_thickness_mm'])*p[2]/m)/2)<1e-5 for p in points)
            if kind=='root':match=on_side and abs(points[0][2]-points[1][2])>h/2 and all(-1e-6<=p[2]<=h+1e-6 and abs(math.hypot(p[0],p[1])-(v['hub_base_diameter_mm']+(v['hub_top_diameter_mm']-v['hub_base_diameter_mm'])*p[2]/h)/2)<1e-5 for p in points)
            else:match=on_side and all(abs(math.hypot(abs(p[0])-cx,p[2]-cz)-v['crown_radius_mm'])<1e-5 for p in points)
            if match:found.append(caps.studs.ref(edge))
        return found
    selected=edges(kind);assert len(selected)==4,('Expected four current wing edges',kind,len(selected))
    result=native.run('fillet.create',radius_mm=v['edge_radius_mm'] if kind=='tip' else v['root_radius_mm'],routes=[dict(edges=[ref]) for ref in selected])['container']
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    return result


def author(native,v,target):
    thread=next(t for t in native.run('thread.catalog',standard='metric',limit=1000)['items'] if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and 'x' not in t['designation'])
    v.update(pitch_mm=thread['pitch_mm'],bore_mm=thread['internal_root_diameter_mm'])
    native.run('new',type='part',name=target.stem)
    ids=dict(wing=wing(native,v));ids['taper']=taper(native,v)
    ids['rounding']={}
    if v['edge_rounding_mode']=='complete':ids['rounding']['tip']=roundings(native,v,'tip')
    ids['hub']=hub(native,v)
    ids['thread']=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='through_all',thread_end='length',
        bore_length_mm=v['hub_height_mm'],thread_length_mm=v['hub_height_mm'],runout_pitch_factor=0,chamfer_enabled=False,drill_point_enabled=False,placement=dict(z=0))['container']
    if v['edge_rounding_mode']!='none':ids['rounding']['root']=roundings(native,v,'root')
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',descriptions=dict(zip(caps.LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    parameters=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},stock={'':thread['designation']},material={'':v['material']},quantity={'':'-'})
    for parameter in parameters:
        if parameter['key'] in values:parameter['values']=values[parameter['key']]
    native.run('document.parameters.set',parameters=parameters)
    native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    stored=configparser.RawConfigParser();stored.read(target,encoding='utf-8')
    entries=json.loads(stored['Document']['dimension_identifiers'])['entries']
    symbol=lambda owner,key:'d'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    dim=lambda item,key:symbol(item['sketch'],'dimension:'+item[key])
    h=ids['hub'];w=ids['wing']
    t=ids['taper']
    relations=['mass = model.mass',f'stock = "M" & {symbol(ids["thread"],"parameter:thread_designation")} & "x" & {symbol(ids["thread"],"parameter:pitch")}',
        f'{dim(w,"root_x")} = {dim(h,"top_radius")}',f'{dim(w,"root_y")} = {dim(h,"height")}',
        f'{dim(w,"landing_x")} = {dim(w,"root_x")} / 8',
        f'{dim(t,"height")} = -{dim(w,"height")}',
        f'{dim(t,"end_height")} = {dim(t,"height")} + 1',
        f'{dim(t,"outside")} = 2 * {dim(t,"root")}',
        f'{symbol(w["feature"],"parameter:side0_length")} = 2 * {dim(t,"root")}',
        f'{symbol(w["feature"],"parameter:profile_offset")} = -{dim(t,"root")}',
        f'{symbol(t["feature"],"parameter:side0_length")} = 2 * ({dim(w,"half_span")} + 1)',
        f'{symbol(t["feature"],"parameter:profile_offset")} = -({dim(w,"half_span")} + 1)',
        f'{symbol(ids["thread"],"parameter:thread_length")} = -{dim(h,"height")}',f'{symbol(ids["thread"],"parameter:bore_length")} = -{dim(h,"height")}']
    native.run('document.relations.set',relations='\n'.join(relations)+'\n');native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    native.run('save');native.run('close');native.run('open',path=str(target))
    mass=next(p['values'][''] for p in native.run('document.parameters.get')['parameters'] if p['key']=='mass');assert float(mass)>0,mass
    v['saved_mass_kg']=float(mass);native.run('close')
    return ids


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',type=Path,required=True);parser.add_argument('--filename')
    parser.add_argument('--edge-rounding',choices=('root','complete','none'),default='root',help='All outputs are unreleased studies; complete also probes the unresolved perimeter rounding')
    args=parser.parse_args()
    stage=args.stage.resolve();completed=[]
    assert stage.is_relative_to(ROOT/'build'),'Unreviewed wing-nut studies must remain under build/'
    selected=[v for v in variants() if not args.filename or v['filename']==args.filename]
    assert selected,'No matching wing-nut study'
    names={v['filename'].casefold() for v in selected}
    assert len(names)==len(selected),'Study filenames collide case-insensitively'
    factory={p.name.casefold() for p in (ROOT/'config/lib/03-ENGINEERING-TABLES').rglob('*.prtz')}
    assert not names.intersection(factory),'An unfinished study would reuse a factory filename'
    stage.mkdir(parents=True,exist_ok=True)
    for variant in selected:
        target=stage/variant['filename'];assert not target.exists();evidence=stage/'evidence'/target.stem
        variant['edge_rounding_mode']=args.edge_rounding
        native=caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:identities=author(native,variant,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(identities,indent=2),encoding='utf-8')
        completed.append(variant);(stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored study and reopened '+target.name,flush=True)


if __name__=='__main__':main()
