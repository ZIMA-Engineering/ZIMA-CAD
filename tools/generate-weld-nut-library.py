"""Author separately reviewed weld-nut families with native referenced projections."""
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
spec=importlib.util.spec_from_file_location('sections',ROOT/'tools/castle-nut-volume.py')
sections=importlib.util.module_from_spec(spec);spec.loader.exec_module(sections)
DATA=ROOT/'tools/data/weld-nut-reviewed-dimensions.json'


def variants():
    data=json.loads(DATA.read_text(encoding='utf-8'));rows=[]
    for kind in ('din','iso'):
        for values in data[kind+'_sizes']:
            base=dict(zip(data[kind+'_columns'],values));d=base['diameter_mm']
            for pitch in [base['pitch_mm'],*data['fine_pitches'].get(str(d),[])]:
                fine=pitch!=base['pitch_mm'];standard='DIN 929' if kind=='din' else 'ISO 21670'
                prefix='HWN-DIN929' if kind=='din' else 'HFWN-ISO21670'
                names=('Šestihranná přivařovací matice','Hexagon weld nut','Sechskant-Schweißmutter',
                    'Écrou hexagonal à souder','Шестигранная приварная гайка') if kind=='din' else (
                    'Šestihranná přivařovací matice s přírubou','Hexagon weld nut with flange','Sechskant-Schweißmutter mit Flansch',
                    'Écrou hexagonal à souder à embase','Шестигранная приварная гайка с фланцем')
                rows.append(dict(base,filename=f'{prefix}-M{d:04.1f}'+(f'x{pitch:04.2f}' if fine else '')+'.prtz',
                    standard=standard,kind=kind,pitch_mm=pitch,fine_pitch=fine,
                    family='05-WELD/'+('01-HEXAGON' if kind=='din' else '02-FLANGE'),
                    names=dict(zip(caps.LANGUAGES,names)),material='Steel',density_kg_mm3=7.85e-6,
                    chamfer_degrees=30,seating_thumbnail=True,representation='Nominal unwelded CAD geometry with schematic thread'))
    assert len(rows)==30 and sum(v['kind']=='din' for v in rows)==16
    return rows


def hexagon(native,v,z,length):
    sk=native.run('sketch.create',name='Hexagon profile',plane='XY')['sketch']
    radius=v['across_flats_mm']/math.sqrt(3)
    native.run('sketch.polygon.create',sketch=sk,center=[0,0],rim=[radius,0],sides=6)
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    items=native.run('sketch.entities',sketch=sk)['items']
    circle=next(x['id'] for x in items if x['kind']=='circle')
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(circle)['center'],'sketch_origin'])
    size=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle],value=2*radius)['dimension']
    tip=next(x['id'] for x in items if x['kind']=='point' and abs(entity(x['id'])['y'])<1e-8 and entity(x['id'])['x']>0)
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[tip],geometry=['sketch_axis:x'])
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    prism=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,profile_plane_offset=z,
        sides=[caps.side(length),caps.side(operation='none')]))['container']
    return dict(sketch=sk,size=size,feature=prism)


def circle_feature(native,diameter,z,length,name,reverse=False):
    sk=native.run('sketch.create',name=name,plane='XY')['sketch']
    circle=native.run('sketch.circle.create',sketch=sk,center=[0,0],radius_mm=diameter/2)['geometry']
    center=native.run('sketch.entity.get',sketch=sk,entity=circle)['entity']['center']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[center,'sketch_origin'])
    size=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle],value=diameter)['dimension']
    sides=[caps.side(length),caps.side(operation='none')]
    if reverse:sides.reverse()
    feature=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,profile_plane_offset=z,sides=sides))['container']
    return dict(sketch=sk,size=size,feature=feature)


def corner_projection(native,v,prism):
    r=v['across_flats_mm']/math.sqrt(3);b=v['projection_depth_mm'];h=v['projection_height_mm']
    sk=native.run('sketch.create',name='Weld projection profile',plane='XY')['sketch']
    points=[[r,0],[r-b,b*math.sqrt(3)],[r-b,-b*math.sqrt(3)]]
    lines=[native.run('sketch.segment.create',sketch=sk,first=p,second=points[(i+1)%3])['geometry'] for i,p in enumerate(points)]
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    source=caps.socket_profile.source_point(native,prism,r,h)
    support=caps.socket_profile.attach(native,sk,source)
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(lines[0])['first'],support])
    feature=native.run('feature.get',container=prism)['feature'];edges=[]
    for edge in native.run('reference.list',kind='edge',owner=prism)['items']:
        if ':'+feature+':end:' not in edge['key']:continue
        packet=native.run('reference.get',kind='edge',**caps.studs.ref(edge))
        ps=[p for segment in packet['segments'] for p in segment['points']]
        if ps and all(abs(p[2]-h)<1e-7 for p in ps) and any(math.dist(p,[r,0,h])<1e-7 for p in ps):edges.append((edge,ps))
    assert len(edges)==2,edges
    references=[support]
    for line,sign in ((lines[0],1),(lines[2],-1)):
        edge=next(e for e,ps in edges if any(p[1]*sign>1e-7 for p in ps))
        reference=native.run('sketch.reference.create',sketch=sk,kind='edge',**caps.studs.ref(edge))['reference'];references.append(reference)
        point=entity(line)['second' if sign==1 else 'first']
        native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[point],geometry=[reference])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[lines[1]])
    depth=native.run('sketch.dimension.create',sketch=sk,kind='distance_x',points=[entity(lines[0])['first'],entity(lines[0])['second']],value=b)['dimension']
    status=native.run('sketch.solve_status',sketch=sk)
    assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
    feature=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,profile_plane_offset=h,
        sides=[caps.side(operation='none'),caps.side(h)]))['container']
    pattern=native.run('pattern.create',source=feature,mode='circular',local_axis='z',count=3,full_circle=True)['object']
    return dict(sketch=sk,depth=depth,feature=feature,pattern=pattern,references=references)


def din_volume(v):
    s,m,h,h2,rp,b,rb,c=[v[k] for k in ('across_flats_mm','height_mm','projection_height_mm',
        'pilot_height_mm','pilot_diameter_mm','projection_depth_mm','bore_mm','mouth_chamfer_mm')]
    rb/=2;rp/=2;r=s/math.sqrt(3);a=s/2;bevel=(r-a)*math.tan(math.pi/6)
    polygon=[(r*math.cos(i*math.pi/3),r*math.sin(i*math.pi/3)) for i in range(6)]
    outer=math.sqrt(3)*s*s/2*(m-h-bevel)
    outer+=sections.integral(lambda z:sections.disk_polygon(polygon,a+z/math.tan(math.pi/6)),0,bevel,1e-9)
    outer+=math.pi*rp*rp*h2+3*math.sqrt(3)*b*b*h
    return outer-math.pi*rb*rb*(m-h+h2)-2*math.pi*(rb*c*c+c**3/3)


def author_din(native,v):
    h=v['projection_height_mm'];m=v['height_mm'];h2=v['pilot_height_mm']
    ids=dict(blank=hexagon(native,v,h,m-h))
    ids['projection']=corner_projection(native,v,ids['blank']['feature'])
    ids['pilot']=circle_feature(native,v['pilot_diameter_mm'],h,h2,'Centering pilot profile',True)
    ids['bevel']=nuts.bevel(native,v,ids['blank']['feature'],True)
    v['bore_start_mm']=h-h2;v['thread_length_mm']=m-h+h2
    return ids


def rounded_flange_projection(native,v,flange):
    R=v['flange_diameter_mm']/2;inner=R-v['projection_depth_mm'];half=v['projection_width_mm']/2;c=v['projection_height_mm']
    outer_y=-math.sqrt(R*R-half*half);inner_y=-math.sqrt(inner*inner-half*half)
    sk=native.run('sketch.create',name='Flange weld projection profile',plane='XY')['sketch']
    outer=native.run('sketch.arc.create',sketch=sk,center=[0,0],start=[-half,outer_y],end=[half,outer_y],clockwise=False)['geometry']
    right=native.run('sketch.segment.create',sketch=sk,first=[half,outer_y],second=[half,inner_y])['geometry']
    inner_arc=native.run('sketch.arc.create',sketch=sk,center=[0,0],start=[half,inner_y],end=[-half,inner_y],clockwise=True)['geometry']
    left=native.run('sketch.segment.create',sketch=sk,first=[-half,inner_y],second=[-half,outer_y])['geometry']
    chord=native.run('sketch.segment.create',sketch=sk,first=[-half,outer_y],second=[half,outer_y],construction=True)['geometry']
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    centers={entity(outer)['center'],entity(inner_arc)['center']}
    for center in centers:native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[center,'sketch_origin'])
    source=caps.circular_edge(native,flange,c,R)
    reference=native.run('sketch.reference.create',sketch=sk,kind='edge',**source)['reference']
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_circle',points=[entity(chord)['first']],geometry=[reference])
    for line in (left,right):native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[line])
    native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[chord])
    # A horizontal chord of the Origin-centered outer arc is already
    # symmetric about Y; a second midpoint constraint is redundant.
    width=native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[chord],value=2*half)['dimension']
    radius=native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[inner_arc],value=inner)['dimension']
    status=native.run('sketch.solve_status',sketch=sk)
    assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
    feature=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,profile_plane_offset=c,
        sides=[caps.side(operation='none'),caps.side(c)]))['container']
    pattern=native.run('pattern.create',source=feature,mode='circular',local_axis='z',count=3,full_circle=True)['object']
    edges={}
    for edge in native.run('reference.list',kind='edge',limit=5000)['items']:
        packet=native.run('reference.get',kind='edge',**caps.studs.ref(edge))
        for segment in packet['segments']:
            points=segment['points']
            if len(points)<3 or any(abs(p[2])>1e-7 for p in points):continue
            radial=math.hypot(*points[0][:2])
            if min(abs(radial-R),abs(radial-inner))>1e-7:continue
            if any(abs(math.hypot(*p[:2])-radial)>1e-7 for p in points):continue
            signature=(round(radial,7),tuple(sorted(tuple(round(x,7) for x in p) for p in (points[0],points[-1]))))
            previous=edges.get(signature)
            if previous is None or len(edge['key'])<len(previous['key']):edges[signature]=edge
    assert len(edges)==6,('Flange projection crown edges missing',edges)
    rounded=native.run('fillet.create',radius_mm=v['projection_radius_mm'],routes=[dict(edges=[caps.studs.ref(edge)]) for edge in edges.values()])['container']
    return dict(sketch=sk,width=width,inner_radius=radius,feature=feature,pattern=pattern,rounding=rounded,reference=reference)


def author_iso(native,v):
    c=v['projection_height_mm'];f=v['flange_height_mm'];m=v['height_mm'];R=v['flange_diameter_mm']/2
    ids=dict(flange=circle_feature(native,2*R,c,f,'Flange profile'))
    back=caps.circular_edge(native,ids['flange']['feature'],c+f,R)
    ids['flange_rounding']=native.run('fillet.create',radius_mm=v['flange_radius_mm'],routes=[dict(edges=[back])])['container']
    ids['blank']=hexagon(native,v,c+f,m-f)
    # The standard limits the pressing root radius to 0.5 mm. Use that bound
    # as an explicit CAD detail and verify its added material independently.
    v['root_radius_mm']=.5
    feature=native.run('feature.get',container=ids['blank']['feature'])['feature'];edges=[]
    for edge in native.run('reference.list',kind='edge',owner=ids['blank']['feature'])['items']:
        if ':'+feature+':end:' not in edge['key']:continue
        packet=native.run('reference.get',kind='edge',**caps.studs.ref(edge))
        points=[p for segment in packet['segments'] for p in segment['points']]
        if points and all(abs(p[2]-c-f)<1e-7 for p in points):edges.append(caps.studs.ref(edge))
    assert len(edges)==6,('Hexagon/flange root edges missing',edges)
    ids['root_rounding']=native.run('fillet.create',radius_mm=v['root_radius_mm'],routes=[dict(edges=edges)])['container']
    ids['bevel']=nuts.bevel(native,dict(v,height_mm=m+c),ids['blank']['feature'],True)
    ids['projection']=rounded_flange_projection(native,v,ids['flange']['feature'])
    v['bore_start_mm']=c;v['thread_length_mm']=m;v['overall_height_mm']=m+c
    return ids


def strip_area(outer,inner,half_width):
    def primitive(radius):return radius*radius*math.asin(half_width/radius)+half_width*math.sqrt(radius*radius-half_width*half_width)
    return primitive(outer)-primitive(inner)


def iso_volume(v):
    s,m,f,R,b,c,g,r1,r2,r0,rb,mouth=[v[k] for k in ('across_flats_mm','height_mm','flange_height_mm','flange_diameter_mm',
        'projection_depth_mm','projection_height_mm','projection_width_mm','projection_radius_mm','flange_radius_mm',
        'root_radius_mm','bore_mm','mouth_chamfer_mm')]
    R/=2;rb/=2;inner=R-b;half=g/2;a=s/2;corner=s/math.sqrt(3);bevel=(corner-a)*math.tan(math.pi/6)
    polygon=[(corner*math.cos(i*math.pi/3),corner*math.sin(i*math.pi/3)) for i in range(6)]
    outer=math.pi*R*R*f+math.sqrt(3)*s*s/2*(m-f-bevel)
    outer+=sections.integral(lambda z:sections.disk_polygon(polygon,a+z/math.tan(math.pi/6)),0,bevel,1e-9)
    A=R-r2
    outer-=math.pi*(R*R*r2-A*A*r2-2*r2**3/3-math.pi*A*r2*r2/2)
    perimeter=2*math.sqrt(3)*s
    # Intersecting fillet cylinders retain the hexagon's mitred corners.
    # A parallel section therefore has area P*t + 6*tan(pi/6)*t^2;
    # the pi*t^2 term of a rounded Minkowski buffer would be incorrect.
    outer+=perimeter*r0*r0*(1-math.pi/4)+2*math.sqrt(3)*r0**3*(5/3-math.pi/2)
    assert r1<=c and 2*r1<b and half<inner
    pad=strip_area(R,inner,half)*(c-r1)
    pad+=sections.integral(lambda t:strip_area(R-r1+r1*math.sin(t),inner+r1-r1*math.sin(t),half)*r1*math.sin(t),0,math.pi/2,1e-9)
    return outer+3*pad-math.pi*rb*rb*m-2*math.pi*(rb*mouth*mouth+mouth**3/3)


def finish(native,v,ids,target):
    thread=next(t for t in native.run('thread.catalog',standard='metric',limit=1000)['items']
        if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and abs(t['pitch_mm']-v['pitch_mm'])<1e-8)
    v['bore_mm']=thread['internal_root_diameter_mm'];c=(v.get('mouth_diameter_mm',v['diameter_mm'])-v['bore_mm'])/2;v['mouth_chamfer_mm']=c
    support_owner=ids['pilot']['feature'] if v['kind']=='din' else ids['flange']['feature']
    supports=[ref for ref,surface in caps.studs.faces(native,support_owner) if surface and surface['kind']=='plane'
        and abs(surface['origin'][2]-v['bore_start_mm'])<1e-7 and surface['axis'][2]*(-1 if surface['reversed'] else 1)<-.99]
    if len(supports)>1:
        feature=native.run('feature.get',container=support_owner)['feature']
        supports=[ref for ref in supports if ':'+feature+':end:' in ref['key']]
    assert len(supports)==1,('Opening source seating face missing',supports)
    hole=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='through_all',thread_end='length',
        bore_length_mm=v['thread_length_mm'],thread_length_mm=v['thread_length_mm'],runout_pitch_factor=0,
        chamfer_enabled=True,chamfer_depth_mm=c,chamfer_angle_degrees=90,drill_point_enabled=False,
        placement=dict(z=v['bore_start_mm']))['container']
    native.run('opening.reference.set',container=hole,index=0,reference=caps.studs.ref(supports[0]),derive_orientation=False)
    ids['thread_support']=caps.studs.ref(supports[0])
    rim=caps.circular_edge(native,hole,v.get('overall_height_mm',v['height_mm']),v['bore_mm']/2)
    ids['mouth']=native.run('chamfer.create',mode='equal_distance',distance_a_mm=c,routes=[dict(edges=[rim])])['container']
    ids['thread']=hole
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),
        dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',
            descriptions=dict(zip(caps.LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    parameters=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},stock={'':thread['designation']},material={'':v['material']},quantity={'':'-'})
    for parameter in parameters:
        if parameter['key'] in values:parameter['values']=values[parameter['key']]
    native.run('document.parameters.set',parameters=parameters);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('document.relations.set',relations='mass = model.mass\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors']
    measured=native.run('body_properties.create')['volume_mm3'];native.run('undo')
    v['independent_volume_mm3']=din_volume(v) if v['kind']=='din' else iso_volume(v)
    assert abs(measured-v['independent_volume_mm3'])<max(1e-6,measured*1e-8),(measured,v)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    relations(native,v,ids,target)
    native.run('regenerate');assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')


def relations(native,v,ids,target):
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8')
    entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    def symbol(owner,key):return 'd'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    hole=ids['thread'];text=f'mass = model.mass\nstock = "M" & {symbol(hole,"parameter:thread_designation")} & "x" & {symbol(hole,"parameter:pitch")}\n'
    if v['kind']=='din':
        blank,pilot,projection=ids['blank'],ids['pilot'],ids['projection']
        height=symbol(blank['feature'],'parameter:side0_length')
        projection_height=symbol(blank['feature'],'parameter:profile_offset')
        pilot_height=symbol(pilot['feature'],'parameter:side1_length')
        for owner in (projection['feature'],pilot['feature']):
            text+=symbol(owner,'parameter:profile_offset')+' = '+projection_height+'\n'
        text+=symbol(projection['feature'],'parameter:side1_length')+' = '+projection_height+'\n'
        length=height+' + '+pilot_height
        text+=symbol(hole,'parameter:thread_length')+' = '+length+'\n'
        text+=symbol(hole,'parameter:bore_length')+' = '+length+'\n'
        profile=symbol(blank['sketch'],'dimension:'+blank['size'])
        text+=symbol(ids['bevel']['sketch'],'dimension:'+ids['bevel']['width'])+' = '+profile+' * (1 - sqrt(3) / 2) / 2\n'
    else:
        blank,flange,projection=ids['blank'],ids['flange'],ids['projection']
        c=symbol(flange['feature'],'parameter:profile_offset');f=symbol(flange['feature'],'parameter:side0_length')
        head=symbol(blank['feature'],'parameter:side0_length');length=head+' + '+f
        text+=symbol(blank['feature'],'parameter:profile_offset')+' = '+c+' + '+f+'\n'
        text+=symbol(projection['feature'],'parameter:profile_offset')+' = '+c+'\n'
        text+=symbol(projection['feature'],'parameter:side1_length')+' = '+c+'\n'
        for key in ('thread_length','bore_length'):text+=symbol(hole,'parameter:'+key)+' = '+length+'\n'
        profile=symbol(blank['sketch'],'dimension:'+blank['size'])
        text+=symbol(ids['bevel']['sketch'],'dimension:'+ids['bevel']['width'])+' = '+profile+' * (1 - sqrt(3) / 2) / 2\n'
    native.run('document.relations.set',relations=text)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--filename');parser.add_argument('--family',choices=('din','iso'),default='din')
    args=parser.parse_args();stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);completed=[]
    for v in variants():
        if v['kind']!=args.family or (args.filename and v['filename']!=args.filename):continue
        target=stage/v['filename'];assert not target.exists(),target
        evidence=stage/'evidence'/target.stem
        native=caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:
            native.run('new',type='part',name=target.stem)
            if v['kind']=='din':ids=author_din(native,v)
            else:ids=author_iso(native,v)
            finish(native,v,ids,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        completed.append(v);(stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('Authored, independently measured and reopened '+target.name,flush=True)


if __name__=='__main__':main()
