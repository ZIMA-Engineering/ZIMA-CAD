"""Stage native DIN 74361 wheel fasteners from reviewed original dimensions.

Candidates stay outside the factory library until independent geometry,
repeated-edit, GUI, persistence and artwork checks have passed.
"""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('spring',ROOT/'tools/generate-spring-washer-library.py')
spring=importlib.util.module_from_spec(spec);spec.loader.exec_module(spring)
caps=spring.caps
spec=importlib.util.spec_from_file_location('sections',ROOT/'tools/castle-nut-volume.py')
sections=importlib.util.module_from_spec(spec);spec.loader.exec_module(sections)
spec=importlib.util.spec_from_file_location('knurled',ROOT/'tools/generate-knurled-nut-library.py')
knurled=importlib.util.module_from_spec(spec);spec.loader.exec_module(knurled)
spec=importlib.util.spec_from_file_location('weld',ROOT/'tools/generate-weld-nut-library.py')
weld=importlib.util.module_from_spec(spec);spec.loader.exec_module(weld)


def variants(kind='washers',washer_bores=(20.5,)):
    facts=json.loads((ROOT/'tools/data/wheel-fastener-reviewed-dimensions.json').read_text(encoding='utf-8'))
    result=[]
    # Explicit selection preserves the previously accepted default assets.
    assert set(washer_bores)<=set(r[0] for r in facts['washer_rows'])
    for row in [r for r in facts['washer_rows'] if r[0] in washer_bores] if kind=='washers' else []:
        v=dict(zip(facts['washer_columns'],row));d=v['bore_mm']
        selected=facts.get('washer_selected_sections',{}).get(format(d,'g'))
        if selected:
            for key in ('sphere_radius_mm','height_mm'):
                nominal=v[key];delta=selected[key]-nominal
                lo,hi=facts['washer_tolerances'][key]
                assert lo<=delta<=hi,(key,nominal,selected)
                v['source_nominal_'+key]=nominal;v[key]=selected[key]
            v['section_selection']=selected['reason']
        v.update(filename=f'SPHW-DIN74361C-D{d:04.1f}.prtz',standard='DIN 74361 C',family='07-SPHERICAL',
            state='compressed',thickness_mm=v['height_mm'],corner_radius_mm=.1,
            split_gap_mm=1,split_angle_degrees=45,density_kg_mm3=7.85e-6,material='Spring steel',
            representation=facts['washer_scope'],source_url=facts['source_url'],
            names=dict(zip(caps.LANGUAGES,('Kulová pružná podložka, stlačený stav',
                'Spherical spring washer, compressed state','Kugelbund-Federring, zusammengedrückt',
                'Rondelle élastique sphérique, état comprimé','Сферическая пружинная шайба, сжатое состояние'))))
        result.append(v)
    for row in facts['type_a_rows'] if kind=='spherical-nuts' else facts['type_f_rows'] if kind=='conical-nuts' else []:
        form='A' if kind=='spherical-nuts' else 'F';v=dict(zip(facts['type_a_columns' if form=='A' else 'type_f_columns'],row))
        d=v['diameter_mm'];suffix='' if form=='A' else f'-A{v["included_angle_degrees"]:g}'
        names=('Kolová matice s kulovým nákružkem','Wheel nut with spherical collar','Radmutter mit Kugelbund',
            'Écrou de roue à collerette sphérique','Колёсная гайка со сферическим буртиком') if form=='A' else (
            'Kolová matice s kuželovým sedlem','Wheel nut with conical seat','Radmutter mit Kegelbund',
            'Écrou de roue à portée conique','Колёсная гайка с коническим седлом')
        v.update(filename=f'WHN-DIN74361{form}-M{d:04.1f}x{v["pitch_mm"]:04.2f}{suffix}.prtz',standard='DIN 74361 '+form,
            family='12-WHEEL/'+('01-SPHERICAL' if form=='A' else '02-CONICAL'),kind=kind,chamfer_degrees=30,
            density_kg_mm3=7.85e-6,material='Steel',source_url=facts['source_url'],representation=facts['scope'],
            names=dict(zip(caps.LANGUAGES,names)))
        result.append(v)
    return result


def origin_axis(native,sk):
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,.123456789],second=[0,1.123456789],construction=True)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    first=native.run('sketch.entity.get',sketch=sk,entity=axis)['entity']['first']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[first,'sketch_origin'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[axis],value=1)
    return axis


def spherical_collar(native,v):
    end,outer,R,h=[v[k] for k in ('end_diameter_mm','flange_diameter_mm','sphere_radius_mm','collar_height_mm')]
    end/=2;outer/=2;zc=math.sqrt(R*R-end*end);edge=zc-math.sqrt(R*R-outer*outer)
    assert 0<edge<h
    sk=native.run('sketch.create',name='Spherical collar meridian',plane='XZ')['sketch']
    p=[[0,0],[end,0],[outer,-edge],[outer,-h],[0,-h]]
    lines={i:native.run('sketch.segment.create',sketch=sk,first=p[i],second=p[(i+1)%5],snap_mm=1e-8)['geometry'] for i in (0,2,3,4)}
    arc=native.run('sketch.arc.create',sketch=sk,center=[0,-zc],start=p[1],end=p[2],clockwise=True,snap_mm=1e-8)['geometry']
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    pts={i:entity(line)['first'] for i,line in lines.items()};pts[1]=entity(lines[0])['second']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[pts[0],'sketch_origin'])
    for i in (0,3):native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[lines[i]])
    for i in (2,4):native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[lines[i]])
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[entity(arc)['center']],geometry=['sketch_axis:y'])
    dims={}
    for key,point,kind,axis,value in [('end',pts[1],'distance_x','y',end),('outside',pts[2],'distance_x','y',outer),('height',pts[3],'distance_y','x',-h)]:
        dims[key]=native.run('sketch.dimension.create',sketch=sk,kind=kind,points=[point],geometry=['sketch_axis:'+axis],value=value)['dimension']
    dims['radius']=native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[arc],value=R)['dimension']
    axis=origin_axis(native,sk);status=native.run('sketch.solve_status',sketch=sk)
    assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
    turn=caps.side(operation='revolution');turn.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,parameters=dict(axis_segment_id=axis,symmetric=False,sides=[turn,caps.side(operation='none')]))['container']
    return dict(sketch=sk,feature=feature,arc=arc,**dims)


def conical_seat(native,v,prism):
    r=v['across_flats_mm']/math.sqrt(3);a=v['end_diameter_mm']/2
    beta=90-v['included_angle_degrees']/2;z=(r-a)*math.tan(math.radians(beta))
    sk=native.run('sketch.create',name='Conical wheel seat meridian',plane='XZ')['sketch']
    p=[[a,0],[r,0],[r,-z]]
    lines=[native.run('sketch.segment.create',sketch=sk,first=q,second=p[(i+1)%3],snap_mm=1e-8)['geometry'] for i,q in enumerate(p)]
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    A,B=entity(lines[0])['first'],entity(lines[0])['second']
    ref=caps.socket_profile.attach(native,sk,caps.socket_profile.source_point(native,prism,r,0))
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[B,ref])
    native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[lines[0]])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[lines[1]])
    end=native.run('sketch.dimension.create',sketch=sk,kind='distance_x',points=[A],geometry=['sketch_axis:y'],value=a)['dimension']
    angle=native.run('sketch.dimension.create',sketch=sk,kind='angle_between',geometry=[lines[0],lines[2]],value=180-beta)['dimension']
    axis=origin_axis(native,sk);status=native.run('sketch.solve_status',sketch=sk)
    assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
    turn=caps.side(operation='revolution');turn.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(axis_segment_id=axis,symmetric=False,sides=[turn,caps.side(operation='none')]))['container']
    return dict(sketch=sk,feature=feature,end=end,angle=angle)


def independent_nut_volume(v):
    s,h,d,rb=[v[k] for k in ('across_flats_mm','height_mm','diameter_mm','bore_mm')];rb/=2
    r=s/math.sqrt(3);a=s/2;area=math.sqrt(3)*s*s/2
    poly=[(r*math.cos(i*math.pi/3),r*math.sin(i*math.pi/3)) for i in range(6)]
    bevel=(r-a)*math.tan(math.radians(v['chamfer_degrees']))
    top=sections.integral(lambda z:sections.disk_polygon(poly,a+(h-z)/math.tan(math.radians(v['chamfer_degrees']))),h-bevel,h,1e-7)
    if v['kind']=='spherical-nuts':
        R=v['sphere_radius_mm'];re=v['end_diameter_mm']/2;ro=v['flange_diameter_mm']/2;hc=v['collar_height_mm'];b=v['clearance_depth_mm']
        zc=math.sqrt(R*R-re*re)
        collar=math.pi*re*re*hc+2*math.pi*((hc-zc)*(ro*ro-re*re)/2+((R*R-re*re)**1.5-(R*R-ro*ro)**1.5)/3)
        volume=collar+area*(h-bevel-hc)+top-math.pi*(v['clearance_diameter_mm']/2)**2*b-math.pi*rb*rb*(h-b)
    else:
        re=v['end_diameter_mm']/2;slope=math.tan(math.radians(v['included_angle_degrees']/2));height=(r-re)/slope
        bottom=sections.integral(lambda z:sections.disk_polygon(poly,re+z*slope),0,height,1e-7)
        volume=bottom+area*(h-bevel-height)+top-math.pi*rb*rb*h
    for c in (v['mouth_chamfer_mm'],v['upper_mouth_chamfer_mm']):volume-=math.pi*(rb*c*c+c**3/3)
    return volume


def author_nut(native,v,target):
    native.run('new',type='part',name=target.stem);ids={}
    if v['kind']=='spherical-nuts':
        ids['collar']=spherical_collar(native,v);offset=v['collar_height_mm']
        ids['blank']=weld.hexagon(native,v,offset,v['height_mm']-offset)
    else:
        ids['blank']=weld.hexagon(native,v,0,v['height_mm'])
        ids['seat']=conical_seat(native,v,ids['blank']['feature'])
    ids['bevel']=weld.nuts.bevel(native,v,ids['blank']['feature'],True)
    thread=next(t for t in native.run('thread.catalog',standard='metric',limit=1000)['items'] if
        abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and abs(t['pitch_mm']-v['pitch_mm'])<1e-8)
    v['bore_mm']=thread['internal_root_diameter_mm'];start=v.get('clearance_depth_mm',0)
    mouth=v['diameter_mm'] if start else v['clearance_diameter_mm']
    v['mouth_chamfer_mm']=(mouth-v['bore_mm'])/2;v['upper_mouth_chamfer_mm']=(v['diameter_mm']-v['bore_mm'])/2
    if start:ids['clearance']=knurled.circle_feature(native,'Lower wheel nut clearance',v['clearance_diameter_mm'],start,subtract=True)
    ids['thread']=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='through_all',thread_end='length',
        bore_length_mm=v['height_mm']-start,thread_length_mm=v['height_mm']-start,runout_pitch_factor=0,
        chamfer_enabled=True,chamfer_depth_mm=v['mouth_chamfer_mm'],chamfer_angle_degrees=90,
        drill_point_enabled=False,placement=dict(z=start))['container']
    if start:
        supports=[ref for ref,surface in caps.studs.faces(native,ids['clearance']['feature']) if surface and surface['kind']=='plane'
            and abs(surface['origin'][2]-start)<1e-7 and abs(abs(surface['axis'][2])-1)<1e-8]
        assert len(supports)==1,('Clearance floor support missing',supports)
        ids['thread_support']=caps.studs.ref(supports[0])
        native.run('opening.reference.set',container=ids['thread'],index=0,reference=ids['thread_support'],derive_orientation=False)
    rim=knurled.circular_edge(native,v['height_mm'],v['bore_mm']/2)
    ids['upper_mouth']=native.run('chamfer.create',mode='equal_distance',distance_a_mm=v['upper_mouth_chamfer_mm'],routes=[dict(edges=[rim])])['container']
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),
        dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',descriptions=dict(zip(caps.LANGUAGES,
            ('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    parameters=native.run('document.parameters.get')['parameters']
    for p in parameters:
        values=dict(name=v['names'],standard={'':v['standard']},stock={'':thread['designation']},material={'':v['material']},quantity={'':'-'})
        if p['key'] in values:p['values']=values[p['key']]
    native.run('document.parameters.set',parameters=parameters);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('document.relations.set',relations='mass = model.mass\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors']
    measured=native.run('body_properties.create')['volume_mm3'];native.run('undo');v['independent_volume_mm3']=independent_nut_volume(v)
    assert abs(measured-v['independent_volume_mm3'])<max(1e-5,measured*1e-8),(measured,v)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8');entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    symbol=lambda owner,key:'d'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    text='mass = model.mass\nstock = "M" & '+symbol(ids['thread'],'parameter:thread_designation')+' & "x" & '+symbol(ids['thread'],'parameter:pitch')+'\n'
    head=symbol(ids['blank']['feature'],'parameter:side0_length')
    if start:
        collar=symbol(ids['collar']['sketch'],'dimension:'+ids['collar']['height'])
        text+=symbol(ids['blank']['feature'],'parameter:profile_offset')+' = -'+collar+'\n'
        length=head+' - '+collar+' - '+symbol(ids['clearance']['feature'],'parameter:side0_length')
    else:length=head
    for key in ('parameter:thread_length','parameter:bore_length'):text+=symbol(ids['thread'],key)+' = '+length+'\n'
    text+=symbol(ids['bevel']['sketch'],'dimension:'+ids['bevel']['width'])+' = '+symbol(ids['blank']['sketch'],'dimension:'+ids['blank']['size'])+' * (1 - sqrt(3) / 2) / 2\n'
    native.run('document.relations.set',relations=text);native.run('regenerate');assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')
    return ids


def independent_washer_volume(v):
    ri,ro,rt=[v[k]/2 for k in ('bore_mm','outside_mm','transition_diameter_mm')]
    h,R,f,g=[v[k] for k in ('height_mm','sphere_radius_mm','corner_radius_mm','split_gap_mm')]
    zc=h-math.sqrt(R*R-rt*rt);edge=zc+math.sqrt(R*R-ro*ro)
    assert 0<f<edge and ri+f<rt<ro<R,(edge,v)
    def half_strip(radius,lo,hi):
        a=max(-radius,min(radius,lo));b=max(-radius,min(radius,hi))
        def primitive(x):return (x*math.sqrt(max(0,radius*radius-x*x))+radius*radius*math.asin(x/radius))/2
        return primitive(b)-primitive(a)
    def area(z):
        outer=ro if z<=edge else math.sqrt(max(0,R*R-(z-zc)**2))
        if z<f:outer=min(outer,ro-f+math.sqrt(max(0,f*f-(z-f)**2)))
        inner=ri if z<=h-f else ri+f-math.sqrt(max(0,f*f-(z-h+f)**2))
        center=h/2-z;half=g/math.sqrt(2)
        removed=half_strip(outer,center-half,center+half)-half_strip(inner,center-half,center+half)
        return math.pi*(outer*outer-inner*inner)-removed
    knots=sorted({0.,f,edge,h-f,h})
    # Cosine substitution removes endpoint square-root singularities from
    # the two independent quarter-circle corner integrations.
    answer=0.
    for a,b in zip(knots,knots[1:]):
        mid=(a+b)/2;span=(b-a)/2
        answer+=sections.integral(lambda t:area(mid-span*math.cos(t))*span*math.sin(t),0,math.pi,1e-7)
    return answer


def washer_meridian(native,v):
    ri,ro,rt=[v[k]/2 for k in ('bore_mm','outside_mm','transition_diameter_mm')]
    h,R=v['height_mm'],v['sphere_radius_mm'];zc=h-math.sqrt(R*R-rt*rt)
    edge=zc+math.sqrt(R*R-ro*ro)
    sk=native.run('sketch.create',name='Spherical washer meridian',plane='XZ')['sketch']
    p=[[ri,0],[ro,0],[ro,-edge],[rt,-h],[ri,-h]]
    lines={i:native.run('sketch.segment.create',sketch=sk,first=p[i],second=p[(i+1)%5],snap_mm=1e-8)['geometry'] for i in (0,1,3,4)}
    arc=native.run('sketch.arc.create',sketch=sk,center=[0,-zc],start=p[2],end=p[3],clockwise=True,snap_mm=1e-8)['geometry']
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    points={i:entity(line)['first'] for i,line in lines.items()};points[2]=entity(lines[1])['second']
    for i in (0,3):native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[lines[i]])
    for i in (1,4):native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[lines[i]])
    for point,axis in ((points[0],'x'),(entity(arc)['center'],'y')):
        native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[point],geometry=['sketch_axis:'+axis])
    drivers={}
    for key,point,kind,axis,value in [('bore',points[0],'distance_x','y',ri),('outside',points[1],'distance_x','y',ro),
        ('transition',points[3],'distance_x','y',rt),('height',points[3],'distance_y','x',-h)]:
        drivers[key]=native.run('sketch.dimension.create',sketch=sk,kind=kind,points=[point],geometry=['sketch_axis:'+axis],value=value)['dimension']
    drivers['radius']=native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[arc],value=R)['dimension']
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,.123456789],second=[0,1.123456789],construction=True)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(axis)['first'],'sketch_origin'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[axis],value=1)
    status=native.run('sketch.solve_status',sketch=sk)
    assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
    turn=caps.side(operation='revolution');turn.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,parameters=dict(axis_segment_id=axis,symmetric=False,sides=[turn,caps.side(operation='none')]))['container']
    return dict(sketch=sk,feature=feature,arc=arc,**drivers)


def author_washer(native,v,target):
    native.run('new',type='part',name=target.stem)
    profile=washer_meridian(native,v)
    # Preserve the tabulated spherical contact. Round the two exposed
    # right-angle corners before the axial split, using a selected CAD R0.1.
    edges=[knurled.circular_edge(native,z,r,profile['feature']) for z,r in ((0,v['outside_mm']/2),(v['height_mm'],v['bore_mm']/2))]
    rounded=native.run('fillet.create',radius_mm=v['corner_radius_mm'],routes=[dict(edges=[e]) for e in edges])['container']
    end_faces=[]
    for z in (0,v['height_mm']):
        candidates=[ref for ref,surface in caps.studs.faces(native,profile['feature']) if surface and surface['kind']=='plane'
            and abs(surface['origin'][2]-z)<1e-7 and abs(abs(surface['axis'][2])-1)<1e-8]
        assert len(candidates)==1,('Spherical washer end face missing',z,candidates)
        end_faces.append(candidates[0])
    split_sk,references=spring.split_profile(native,v,profile['feature'],end_faces=end_faces)
    split=native.run('feature.create',sketch=split_sk,combine='subtract',parameters=dict(symmetric=False,
        sides=[caps.side(v['height_mm'],extent='through_all'),caps.side(operation='none')]))['container']
    axis_sk=native.run('sketch.create',name='Washer datum axis',plane='XY')['sketch']
    forward=caps.side(v['height_mm']);forward['axis_enabled']=True
    reverse=caps.side(operation='none');reverse['axis_enabled']=False
    datum=native.run('feature.create',sketch=axis_sk,parameters=dict(type='axis',symmetric=False,sides=[forward,reverse]))['container']
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),
        dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',descriptions=dict(zip(caps.LANGUAGES,
            ('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    parameters=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},stock={'':f'Ø{v["bore_mm"]:g}'},material={'':v['material']},quantity={'':'-'})
    for p in parameters:
        if p['key'] in values:p['values']=values[p['key']]
    native.run('document.parameters.set',parameters=parameters);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('document.relations.set',relations='mass = model.mass\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors']
    measured=native.run('body_properties.create')['volume_mm3'];native.run('undo')
    v['independent_volume_mm3']=independent_washer_volume(v)
    assert abs(measured-v['independent_volume_mm3'])<max(1e-5,measured*1e-8),(measured,v)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8');entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    symbol=lambda key:'d'+str(next(e['number'] for e in entries if e['owner']==profile['sketch'] and e['key']=='dimension:'+profile[key]))
    text='mass = model.mass\nstock = "Ø" & (2 * '+symbol('bore')+') & "x" & (2 * '+symbol('outside')+') & "x" & (-'+symbol('height')+')\n'
    axis_length='d'+str(next(e['number'] for e in entries if e['owner']==datum and e['key']=='parameter:side0_length'))
    text+=axis_length+' = -'+symbol('height')+'\n'
    native.run('document.relations.set',relations=text);native.run('regenerate')
    assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')
    return dict(profile=profile,rounded=rounded,split_sketch=split_sk,split=split,split_references=references,datum_axis=datum)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--filename');parser.add_argument('--kind',choices=('washers','spherical-nuts','conical-nuts'),default='washers')
    parser.add_argument('--washer-bore',type=float,action='append',help='Explicit reviewed source bore; defaults to 20.5 mm')
    args=parser.parse_args();stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);completed=[]
    for v in variants(args.kind,args.washer_bore or (20.5,)):
        if args.filename and args.filename!=v['filename']:continue
        target=stage/v['filename'];assert not target.exists(),target
        evidence=stage/'evidence'/target.stem
        native=caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=author_washer(native,v,target) if args.kind=='washers' else author_nut(native,v,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        completed.append(v);(stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)


if __name__=='__main__':main()
