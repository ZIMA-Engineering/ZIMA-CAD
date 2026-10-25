"""Stage historical pressed wing nuts with explicit native profile radii."""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('wing',ROOT/'tools/generate-wing-nut-library.py')
wing=importlib.util.module_from_spec(spec);spec.loader.exec_module(wing)
caps=wing.caps


def variants():
    source=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))['05-NUTS/09-WING']
    table=source['tables'][0];number=lambda x:float(x.replace(',','.'));rows=[]
    for col in range(1,7):
        d=number(table[0][col][1:]);v=dict(filename=f'PWN-CSN021665-M{d:04.1f}.prtz',standard='ČSN 02 1665',diameter_mm=d,
            hub_top_diameter_mm=number(table[1][col]),hub_base_diameter_mm=number(table[2][col]),span_mm=number(table[3][col]),
            tip_thickness_mm=number(table[4][col]),root_thickness_mm=number(table[5][col]),height_mm=number(table[6][col]),
            hub_height_mm=number(table[7][col]),outside_radius_mm=number(table[8][col]),hub_radius_mm=number(table[12][col]),
            inner_angle_degrees=70,source_sha256=source['sha256'],density_kg_mm3=7.85e-6,material='Steel',
            family='09-WING/01-PRESSED',review_status='unreleased pressed wing candidate',
            names=dict(zip(caps.LANGUAGES,('Křídlová matice lisovaná za studena','Cold-pressed wing nut','Kaltgepresste Flügelmutter',
                'Écrou à oreilles pressé à froid','Холодноштампованная барашковая гайка'))))
        # The user's accepted M10 remodel seats the wing root at 9.0 mm inside
        # the 10.35 mm hub radius. Scale that authored containment ratio for
        # the other nominal hub sizes; the lower tangent is then derived by
        # the same rounded profile construction instead of a driving angle.
        v['wing_root_radius_mm']=v['hub_base_diameter_mm']/2*(9.0/10.35)
        v['wing_root_x_mm']=v['hub_top_diameter_mm']/2
        v['wing_root_height_mm']=v['hub_height_mm']
        v['wing_thickness_mm']=v['root_thickness_mm']
        v['root_tangent_degrees']=65.69165155770312
        rows.append(v)
    return rows


def profile_geometry(v,legacy=False):
    R=v['outside_radius_mm'];a=[v['hub_base_diameter_mm']/2 if legacy else v['wing_root_radius_mm'],0]
    q=[v['hub_top_diameter_mm']/2,v['hub_height_mm']] if legacy else [v['wing_root_x_mm'],v['wing_root_height_mm']]
    alpha=math.radians(v['inner_angle_degrees']);beta=math.radians(70 if legacy else v['root_tangent_degrees'])
    u=[q[0]+(v['height_mm']-q[1])/math.tan(alpha),v['height_mm']]
    t=[v['span_mm']/2,v['height_mm']-(v['span_mm']/2-u[0])/math.tan(alpha)]
    c=[t[0]-R,t[1]];delta=[a[i]-c[i] for i in range(2)];normal=[math.sin(beta),-math.cos(beta)]
    radius=(R*R-sum(x*x for x in delta))/(2*(sum(x*y for x,y in zip(delta,normal))-R))
    assert radius>0,(v,radius)
    lower=[a[i]+radius*normal[i] for i in range(2)]
    join=[(radius*c[i]+R*lower[i])/(radius+R) for i in range(2)]
    assert 0<join[1]<t[1]<u[1] and q[0]<u[0]<t[0],(v,join,t,u)
    return dict(a=a,q=q,u=u,t=t,c=c,lower=lower,join=join,lower_radius=radius)


def pressed_wing(native,v):
    # Start from the previously verified fully constrained profile, then apply
    # the same deletions and replacement geometry as the accepted user M10.
    # This lets the solver retain its valid branch while removing the obsolete
    # lower-angle and diagonal-root drivers.
    g=profile_geometry(v,legacy=True);a,q,u,t,c,lower,join=[g[k] for k in ('a','q','u','t','c','lower','join')]
    R=v['outside_radius_mm'];landing=[-q[0]/8,q[1]];axis=[0,q[1]];origin=[0,0];local=lambda p:[p[0],-p[1]]
    sk=native.run('sketch.create',name='Pressed wing outline',plane='XZ')['sketch']
    low=native.run('sketch.arc.create',sketch=sk,center=local(lower),start=local(a),end=local(join),clockwise=False)['geometry']
    arc=native.run('sketch.arc.create',sketch=sk,center=local(c),start=local(join),end=local(t),clockwise=True,snap_mm=1e-8)['geometry']
    top=native.run('sketch.segment.create',sketch=sk,first=local(t),second=local(u),snap_mm=1e-8)['geometry']
    inner=native.run('sketch.segment.create',sketch=sk,first=local(u),second=local(q),snap_mm=1e-8)['geometry']
    flat=native.run('sketch.segment.create',sketch=sk,first=local(q),second=local(landing),snap_mm=1e-8)['geometry']
    closing=native.run('sketch.segment.create',sketch=sk,first=local(landing),second=local(a),snap_mm=1e-8)['geometry']
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    e=entity(arc);p=entity(top)['second'];root=entity(inner)['second'];start=entity(closing)['second'];center=e['center']
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[start],geometry=['sketch_axis:x'])
    native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[flat])
    dims={}
    dims['radius']=wing.dimension(native,sk,'radius',geometry=[arc],value=R)
    maximum=entity(top)['first']
    native.run('sketch.constraint.create',sketch=sk,kind='horizontal',points=[maximum,center])
    dims['span']=wing.dimension(native,sk,'distance_x',points=[maximum],geometry=['sketch_axis:y'],value=v['span_mm']/2)
    for key,point,kind,axis_name,value in [('base',start,'distance_x','y',a[0]),('root_x',root,'distance_x','y',q[0]),
        ('root_y',root,'distance_y','x',-q[1]),('landing',entity(flat)['second'],'distance_x','y',landing[0]),
        ('height',p,'distance_y','x',-v['height_mm'])]:
        dims[key]=wing.dimension(native,sk,kind,points=[point],geometry=['sketch_axis:'+axis_name],value=value)
    dims['angle']=wing.dimension(native,sk,'angle_between',geometry=['sketch_axis:x',inner],value=180-v['inner_angle_degrees'])
    native.run('sketch.constraint.create',sketch=sk,kind='perpendicular',geometry=[inner,top])
    shared=next(point for point in (entity(low)['start'],entity(low)['end']) if point in (e['start'],e['end']))
    native.run('sketch.constraint.create',sketch=sk,kind='tangent',geometry=[low,arc],points=[shared])
    beta=math.radians(70)
    helper=native.run('sketch.segment.create',sketch=sk,first=local(a),second=[a[0]+math.cos(beta),-math.sin(beta)],construction=True,snap_mm=1e-8)['geometry']
    helper_length=wing.dimension(native,sk,'distance',geometry=[helper],value=1)
    dims['lower_angle']=wing.dimension(native,sk,'angle_between',geometry=['sketch_axis:x',helper],value=70)
    helper_tangent=native.run('sketch.constraint.create',sketch=sk,kind='tangent',geometry=[helper,low],points=[start])['constraint']
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0

    landing_point=entity(flat)['second']
    native.run('sketch.constraint.delete',sketch=sk,constraint=helper_tangent)
    native.run('sketch.dimension.delete',sketch=sk,dimension=dims.pop('lower_angle'))
    native.run('sketch.dimension.delete',sketch=sk,dimension=helper_length)
    native.run('sketch.geometry.delete',sketch=sk,geometry=helper)
    native.run('sketch.dimension.delete',sketch=sk,dimension=dims.pop('landing'))
    native.run('sketch.geometry.delete',sketch=sk,geometry=closing)
    native.run('sketch.dimension.set',sketch=sk,dimension=dims['base'],value=v['wing_root_radius_mm'])
    point=entity(landing_point);target=local(axis)
    native.run('sketch.translate',sketch=sk,points=[landing_point],delta=[target[0]-point['x'],target[1]-point['y']])
    axis_side=native.run('sketch.segment.create',sketch=sk,first=target,second=local(origin),snap_mm=1e-8)['geometry']
    closing=native.run('sketch.segment.create',sketch=sk,first=local([v['wing_root_radius_mm'],0]),second=local(origin),snap_mm=1e-8)['geometry']
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[entity(axis_side)['first']],geometry=['sketch_axis:y'])
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(axis_side)['second'],'sketch_origin'])
    status=native.run('sketch.solve_status',sketch=sk);assert status['remaining_degrees_of_freedom']==1,status
    dims['lower_angle']=add_root_tangent_driver(native,sk,low)
    status=native.run('sketch.solve_status',sketch=sk);assert status['remaining_degrees_of_freedom']==0,status
    feature=native.run('feature.create',sketch=sk,parameters=dict(symmetric=True,
        sides=[caps.side(v['root_thickness_mm']),caps.side(operation='none')]))['container']
    pattern=native.run('pattern.create',source=feature,mode='circular',local_axis='z',count=2,full_circle=True)['object']
    return dict(sketch=sk,feature=feature,pattern=pattern,arc=arc,lower_arc=low,dimensions=dims)


def pressed_hub(native,v):
    a=[v['hub_base_diameter_mm']/2,0];b=[v['hub_top_diameter_mm']/2,v['hub_height_mm']];R=v['hub_radius_mm']
    dx,dz=b[0]-a[0],b[1]-a[1];chord=math.hypot(dx,dz);height=math.sqrt(R*R-chord*chord/4)
    center=[(a[0]+b[0])/2-dz/chord*height,(a[1]+b[1])/2+dx/chord*height]
    sk=native.run('sketch.create',name='Pressed hub meridian',plane='XZ')['sketch']
    # Keep the axisymmetric hub's periodic seam out of the wing-profile plane.
    # Coincidence there can make OCCT drop valid material during Boolean Add
    # after successive edits. This changes the construction seam, not the solid.
    native.run('sketch.set',sketch=sk,placement=dict(rotation_z=90))
    bottom=native.run('sketch.segment.create',sketch=sk,first=[0,0],second=[a[0],0])['geometry']
    arc=native.run('sketch.arc.create',sketch=sk,center=[center[0],-center[1]],start=[a[0],0],end=[b[0],-b[1]],clockwise=True,snap_mm=1e-8)['geometry']
    top=native.run('sketch.segment.create',sketch=sk,first=[b[0],-b[1]],second=[0,-b[1]],snap_mm=1e-8)['geometry']
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,-b[1]],second=[0,0],snap_mm=1e-8)['geometry']
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(bottom)['first'],'sketch_origin'])
    for line in (bottom,top):native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[line])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    dims={}
    for key,point,kind,axis_name,value in [('base',entity(bottom)['second'],'distance_x','y',a[0]),
        ('top',entity(top)['first'],'distance_x','y',b[0]),('height',entity(top)['first'],'distance_y','x',-b[1])]:
        dims[key]=wing.dimension(native,sk,kind,points=[point],geometry=['sketch_axis:'+axis_name],value=value)
    dims['radius']=wing.dimension(native,sk,'radius',geometry=[arc],value=R)
    # A distinct centerline is used so the ordinary profile's axis boundary stays solid.
    helper=native.run('sketch.segment.create',sketch=sk,first=[0,0],second=[0,1],construction=True,snap_mm=1e-8)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=helper,centerline=True)
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[helper])
    wing.dimension(native,sk,'distance',geometry=[helper],value=1)
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    side=caps.side(operation='revolution');side.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,parameters=dict(axis_segment_id=helper,symmetric=False,sides=[side,caps.side(operation='none')]))['container']
    return dict(sketch=sk,feature=feature,dimensions=dims)


def independent_volume(v):
    g=profile_geometry(v);R=v['outside_radius_mm'];cx,cz=g['c'];a=v['hub_base_diameter_mm']/2
    rh=v['hub_radius_mm'];b=v['hub_top_diameter_mm']/2;hub=v['hub_height_mm'];height=v['height_mm']
    wing_root=v['wing_root_height_mm'];wing_x=v['wing_root_x_mm']
    alpha=math.radians(v['inner_angle_degrees']);ux=g['u'][0];tx,tz=g['t'];lower=g['lower'];rl=g['lower_radius'];jz=g['join'][1]
    hx,hz=b-a,hub;chord=math.hypot(hx,hz);offset=math.sqrt(rh*rh-chord*chord/4)
    hc=(a+b)/2-hz/chord*offset;zc=hub/2+hx/chord*offset
    def area(z):
        if z<jz:right=lower[0]-math.sqrt(max(0,rl*rl-(z-lower[1])**2))
        elif z<=tz:right=cx+math.sqrt(max(0,R*R-(z-cz)**2))
        else:right=ux+(tx-ux)*(height-z)/(height-tz)
        left=0 if z<wing_root else wing_x+(z-wing_root)/math.tan(alpha)
        lo=max(0,left)
        tapered=v['root_thickness_mm']+(v['tip_thickness_mm']-v['root_thickness_mm'])*z/v['taper_height_mm']
        width=tapered/2
        rectangle=2*width*max(0,right-lo)
        if z>hub:return 2*rectangle
        r=hc+math.sqrt(max(0,rh*rh-(z-zc)**2));hi=min(r,right);overlap=0
        if hi>lo:
            cross=math.sqrt(max(0,r*r-width*width))
            straight=max(lo,min(hi,cross));overlap=2*width*(straight-lo)
            def segment(x):return x*math.sqrt(max(0,r*r-x*x))+r*r*math.asin(max(-1,min(1,x/r)))
            overlap+=segment(hi)-segment(straight)
        return math.pi*r*r+2*(rectangle-overlap)-math.pi*(v['bore_mm']/2)**2
    def integrate(zlo,zhi,tol):
        # A cosine change of variable removes the circle/rectangle contact's
        # square-root endpoint singularity. Endpoint weights are exactly zero.
        def sample(theta):
            if theta==0 or theta==math.pi/2:return 0.
            sn,cs=math.sin(theta),math.cos(theta)
            return area(zlo+(zhi-zlo)*sn*sn)*2*(zhi-zlo)*sn*cs
        def recurse(a,b,fa,fm,fb,whole,tol,depth):
            m=(a+b)/2;l=(a+m)/2;r=(m+b)/2;fl,fr=sample(l),sample(r)
            left=(m-a)*(fa+4*fl+fm)/6;right=(b-m)*(fm+4*fr+fb)/6
            if abs(left+right-whole)<=15*tol:return left+right+(left+right-whole)/15
            assert depth>0,'Independent pressed-wing section integral did not converge'
            return recurse(a,m,fa,fl,fm,left,tol/2,depth-1)+recurse(m,b,fm,fr,fb,right,tol/2,depth-1)
        a,b=0.,math.pi/2;m=(a+b)/2;fm=sample(m)
        return recurse(a,b,0.,fm,0.,(b-a)*4*fm/6,tol,36)
    breaks=sorted({0.,hub,height,*[x for x in
        (wing_root,v['taper_height_mm'],jz,cz,tz) if 0<x<height]})
    return sum(integrate(a,b,1e-9) for a,b in zip(breaks,breaks[1:]))


def relations(native,target,ids):
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8');entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    symbol=lambda owner,key:'d'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    lines=['mass = model.mass',f'stock = "M" & {symbol(ids["thread"],"parameter:thread_designation")} & "x" & {symbol(ids["thread"],"parameter:pitch")}',
        ]
    native.run('document.relations.set',relations='\n'.join(lines)+'\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors'];native.run('save')


def author(native,v,target):
    native.run('cd',path=str(target.parent));native.run('new',type='part',name=target.stem)
    ids=dict(wing=pressed_wing(native,v),taper=wing.taper(native,v),hub=pressed_hub(native,v))
    thread=next(t for t in native.run('thread.catalog',standard='metric',limit=1000)['items'] if t['nominal_diameter_mm']==v['diameter_mm'] and 'x' not in t['designation'])
    v.update(pitch_mm=thread['pitch_mm'],bore_mm=thread['internal_root_diameter_mm'])
    ids['thread']=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='through_all',thread_end='length',
        bore_length_mm=v['hub_height_mm'],thread_length_mm=v['hub_height_mm'],runout_pitch_factor=0,chamfer_enabled=False,drill_point_enabled=False,placement=dict(z=0))['container']
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',
        descriptions=dict(zip(caps.LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    rows=native.run('document.parameters.get')['parameters'];values=dict(name=v['names'],standard={'':v['standard']},material={'':v['material']},stock={'':thread['designation']},quantity={'':'-'})
    for row in rows:
        if row['key'] in values:row['values']=values[row['key']]
    native.run('document.parameters.set',parameters=rows);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('document.relations.set',relations='mass = model.mass\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close');native.run('open',path=str(target))
    relations(native,target,ids)
    v['independent_volume_mm3']=independent_volume(v)
    properties=native.run('body_properties.create');native.run('undo');volume=properties['volume_mm3']
    # Use the established factory checker's one-ppm nominal-section criterion.
    # The native document still has its normal 0.001 mm modeling tolerance;
    # BRep adaptive-integral/cache agreement is checked separately at 1e-8.
    # Retain the discrepancy; never adjust density or native precision to fit.
    v['independent_volume_difference_mm3']=volume-v['independent_volume_mm3']
    v['independent_volume_bound_mm3']=max(1e-5,volume*1e-6)
    assert abs(v['independent_volume_difference_mm3'])<=v['independent_volume_bound_mm3'],(volume,v['independent_volume_mm3'])
    v['saved_mass_kg']=float(next(p['values'][''] for p in native.run('document.parameters.get')['parameters'] if p['key']=='mass'));assert v['saved_mass_kg']>0
    native.run('close');return ids


def sketch_entities(native,sketch):
    result={}
    for item in native.run('sketch.entities',sketch=sketch,limit=1000)['items']:
        value=native.run('sketch.entity.get',sketch=sketch,entity=item['id'])['entity']
        value['_collection']=item['kind'];result[item['id']]=value
    return result


def one(items,description):
    assert len(items)==1,(description,items)
    return items[0]


def dimension_id(entities,kind,value,point=None,tolerance=1e-7):
    return one([key for key,item in entities.items() if item['_collection']=='dimension' and item['kind']==kind and
        abs(item['value']-value)<=tolerance and (point is None or item['first']==point)],f'{kind}={value} at {point}')


def discover_ids(native,v):
    sketches={item['name']:item for item in native.run('sketch.list',limit=100)['items']}
    wing_sketch=sketches['Pressed wing outline'];wing_entities=sketch_entities(native,wing_sketch['sketch'])
    arcs=[item for item in wing_entities.values() if item['_collection']=='arc']
    outer=one([item for item in arcs if abs(item['radius']-v['outside_radius_mm'])<1e-7],'outer wing arc')
    low=one([item for item in arcs if item['id']!=outer['id']],'lower wing arc')
    helper=one([item for item in wing_entities.values() if item['_collection']=='segment' and item['construction']],
        'lower tangent helper')
    base=one([point for point in (low['start'],low['end']) if abs(wing_entities[point]['y'])<1e-7],'wing base point')
    dimensions=dict(
        radius=dimension_id(wing_entities,'radius',v['outside_radius_mm']),
        span=dimension_id(wing_entities,'distance_x',v['span_mm']/2),
        base=dimension_id(wing_entities,'distance_x',v['wing_root_radius_mm'],base),
        root_x=dimension_id(wing_entities,'distance_x',v['hub_top_diameter_mm']/2),
        root_y=dimension_id(wing_entities,'distance_y',-v['hub_height_mm']),
        height=dimension_id(wing_entities,'distance_y',-v['height_mm']),
        angle=dimension_id(wing_entities,'angle_between',180-v['inner_angle_degrees']))
    dimensions['lower_angle']=one([key for key,item in wing_entities.items() if item['_collection']=='dimension' and
        item['kind']=='angle_between' and helper['id'] in (item.get('geometry'),item.get('second_geometry'))],
        'lower tangent angle dimension')
    taper=sketches['Wing thickness taper'];taper_entities=sketch_entities(native,taper['sketch'])
    taper_ids=dict(sketch=taper['sketch'],feature=taper['owner'],
        root=dimension_id(taper_entities,'distance_x',v['root_thickness_mm']/2),
        tip=dimension_id(taper_entities,'distance_x',v['tip_thickness_mm']/2),
        height=dimension_id(taper_entities,'distance_y',v['height_mm']),
        end_height=dimension_id(taper_entities,'distance_y',v['height_mm']+1),
        outside=dimension_id(taper_entities,'distance_x',v['root_thickness_mm']))
    hub=sketches['Pressed hub meridian'];hub_entities=sketch_entities(native,hub['sketch'])
    hub_ids=dict(sketch=hub['sketch'],feature=hub['owner'],dimensions=dict(
        base=dimension_id(hub_entities,'distance_x',v['hub_base_diameter_mm']/2),
        top=dimension_id(hub_entities,'distance_x',v['hub_top_diameter_mm']/2),
        height=dimension_id(hub_entities,'distance_y',-v['hub_height_mm']),
        radius=dimension_id(hub_entities,'radius',v['hub_radius_mm'])))
    history=native.run('history.list')['items'];thread=history[-1]['object']
    return dict(wing=dict(sketch=wing_sketch['sketch'],feature=wing_sketch['owner'],arc=outer['id'],lower_arc=low['id'],dimensions=dimensions),
        taper=taper_ids,hub=hub_ids,thread=thread)


def capture_authored_profile(native,v,ids):
    entities=sketch_entities(native,ids['wing']['sketch']);low=entities[ids['wing']['lower_arc']]
    base=one([entities[key] for key in (low['start'],low['end']) if abs(entities[key]['y'])<1e-7],'authored wing base')
    center=entities[low['center']];dx=center['x']-base['x'];dz=-center['y']+base['y']
    v['wing_root_radius_mm']=base['x']
    v['root_radius_mm']=low['radius']
    v['root_tangent_degrees']=math.degrees(math.atan2(dx,-dz))
    expected=profile_geometry(v)
    assert abs(expected['lower_radius']-low['radius'])<1e-6,(expected['lower_radius'],low['radius'])


def accept_existing(native,v,target):
    ids=discover_ids(native,v);capture_authored_profile(native,v,ids)
    v['wing_root_x_mm']=abs(native.run('sketch.dimension.get',sketch=ids['wing']['sketch'],
        dimension=ids['wing']['dimensions']['root_x'])['value'])
    v['wing_root_height_mm']=abs(native.run('sketch.dimension.get',sketch=ids['wing']['sketch'],
        dimension=ids['wing']['dimensions']['root_y'])['value'])
    v['wing_thickness_mm']=native.run('feature.get',container=ids['wing']['feature'])['parameters']['sides'][0]['length']
    v['taper_height_mm']=abs(native.run('sketch.dimension.get',sketch=ids['taper']['sketch'],dimension=ids['taper']['height'])['value'])
    opening=native.run('opening.get',container=ids['thread']);v.update(pitch_mm=opening['pitch_mm'],bore_mm=opening['bore_diameter_mm'],
        opening_thread_length_mm=opening['thread_length_mm'])
    v['independent_volume_mm3']=independent_volume(v)
    properties=native.run('body_properties.create');native.run('undo');volume=properties['volume_mm3']
    v['independent_volume_difference_mm3']=volume-v['independent_volume_mm3']
    v['independent_volume_bound_mm3']=max(1e-5,volume*1e-6)
    assert abs(v['independent_volume_difference_mm3'])<=v['independent_volume_bound_mm3'],(volume,v['independent_volume_mm3'])
    v['saved_mass_kg']=float(next(p['values'][''] for p in native.run('document.parameters.get')['parameters'] if p['key']=='mass'))
    assert v['saved_mass_kg']>0
    return ids


def add_root_tangent_driver(native,sketch,low_id):
    entities=sketch_entities(native,sketch);low=entities[low_id]
    base=one([point for point in (low['start'],low['end']) if abs(entities[point]['y'])<1e-7],'wing base point')
    point=entities[base];center=entities[low['center']]
    beta=math.degrees(math.atan2(center['x']-point['x'],center['y']-point['y']))
    direction=math.radians(beta)
    helper=native.run('sketch.segment.create',sketch=sketch,first=[point['x'],point['y']],
        second=[point['x']+math.cos(direction),point['y']-math.sin(direction)],construction=True,snap_mm=1e-8)['geometry']
    helper_entity=native.run('sketch.entity.get',sketch=sketch,entity=helper)['entity'];assert helper_entity['first']==base
    wing.dimension(native,sketch,'distance',geometry=[helper],value=1)
    angle=wing.dimension(native,sketch,'angle_between',geometry=['sketch_axis:x',helper],value=beta)
    native.run('sketch.constraint.create',sketch=sketch,kind='tangent',geometry=[helper,low_id],points=[base])
    return angle


def stabilize_reference(native,v,target):
    native.run('open',path=str(target))
    sketch=next(item for item in native.run('sketch.list',limit=100)['items'] if item['name']=='Pressed wing outline')
    entities=sketch_entities(native,sketch['sketch']);arcs=[item for item in entities.values() if item['_collection']=='arc']
    outer=one([item for item in arcs if abs(item['radius']-v['outside_radius_mm'])<1e-7],'outer wing arc')
    low=one([item for item in arcs if item['id']!=outer['id']],'lower wing arc')
    helpers=[item for item in entities.values() if item['_collection']=='segment' and item['construction']]
    if not helpers:add_root_tangent_driver(native,sketch['sketch'],low['id'])
    status=native.run('sketch.solve_status',sketch=sketch['sketch']);assert status['remaining_degrees_of_freedom']==0,status
    native.run('regenerate');assert not native.run('history.list')['calculation_errors'];native.run('save')
    ids=accept_existing(native,v,target);native.run('close');return ids


def remodel(native,v,source,target):
    shutil.copy2(source,target);native.run('open',path=str(target))
    relations=[line for line in native.run('document.relations.get')['relations'].splitlines() if line.strip()][:2]
    assert relations and relations[0]=='mass = model.mass' and relations[1].startswith('stock = '),relations
    native.run('document.relations.set',relations='\n'.join(relations)+'\n')
    sketch=next(item for item in native.run('sketch.list',limit=100)['items'] if item['name']=='Pressed wing outline')
    entities=sketch_entities(native,sketch['sketch']);arcs=[item for item in entities.values() if item['_collection']=='arc']
    outer=one([item for item in arcs if abs(item['radius']-v['outside_radius_mm'])<1e-7],'outer wing arc')
    low=one([item for item in arcs if item['id']!=outer['id']],'lower wing arc')
    base=one([point for point in (low['start'],low['end']) if abs(entities[point]['y'])<1e-7],'wing base point')
    helper=one([item for item in entities.values() if item['_collection']=='segment' and item['construction']],'lower tangent helper')
    closing=one([item for item in entities.values() if item['_collection']=='segment' and not item['construction'] and base in (item['first'],item['second'])],'old root closing segment')
    landing=closing['second'] if closing['first']==base else closing['first']
    helper_constraints=[item['id'] for item in entities.values() if item['_collection']=='constraint' and helper['id'] in (item.get('geometry'),item.get('second_geometry'))]
    helper_points={helper['first'],helper['second']}
    helper_dimensions=[item['id'] for item in entities.values() if item['_collection']=='dimension' and
        (helper['id'] in (item.get('geometry'),item.get('second_geometry')) or
         (item['kind']=='distance' and {item.get('first'),item.get('second')}==helper_points))]
    landing_dimension=one([item['id'] for item in entities.values() if item['_collection']=='dimension' and item['kind']=='distance_x' and item['first']==landing],'landing dimension')
    base_dimension=dimension_id(entities,'distance_x',v['hub_base_diameter_mm']/2,base)
    for constraint in helper_constraints:native.run('sketch.constraint.delete',sketch=sketch['sketch'],constraint=constraint)
    for dimension in helper_dimensions:native.run('sketch.dimension.delete',sketch=sketch['sketch'],dimension=dimension)
    native.run('sketch.geometry.delete',sketch=sketch['sketch'],geometry=helper['id'])
    native.run('sketch.dimension.delete',sketch=sketch['sketch'],dimension=landing_dimension)
    native.run('sketch.geometry.delete',sketch=sketch['sketch'],geometry=closing['id'])
    native.run('sketch.dimension.set',sketch=sketch['sketch'],dimension=base_dimension,value=v['wing_root_radius_mm'])
    point=native.run('sketch.entity.get',sketch=sketch['sketch'],entity=landing)['entity'];axis_target=[0,-v['hub_height_mm']]
    native.run('sketch.translate',sketch=sketch['sketch'],points=[landing],delta=[axis_target[0]-point['x'],axis_target[1]-point['y']])
    axis_side=native.run('sketch.segment.create',sketch=sketch['sketch'],first=axis_target,second=[0,0],snap_mm=1e-8)['geometry']
    axis_entity=native.run('sketch.entity.get',sketch=sketch['sketch'],entity=axis_side)['entity']
    assert axis_entity['first']==landing
    base_point=native.run('sketch.entity.get',sketch=sketch['sketch'],entity=base)['entity']
    origin_point=native.run('sketch.entity.get',sketch=sketch['sketch'],entity=axis_entity['second'])['entity']
    new_closing=native.run('sketch.segment.create',sketch=sketch['sketch'],first=[base_point['x'],base_point['y']],
        second=[origin_point['x'],origin_point['y']],snap_mm=1e-6)['geometry']
    closing_entity=native.run('sketch.entity.get',sketch=sketch['sketch'],entity=new_closing)['entity']
    assert {closing_entity['first'],closing_entity['second']}=={base,axis_entity['second']},closing_entity
    native.run('sketch.constraint.create',sketch=sketch['sketch'],kind='point_on_line',points=[axis_entity['first']],geometry=['sketch_axis:y'])
    native.run('sketch.constraint.create',sketch=sketch['sketch'],kind='point_reference',points=[axis_entity['second'],'sketch_origin'])
    status=native.run('sketch.solve_status',sketch=sketch['sketch']);assert status['remaining_degrees_of_freedom']==1,status
    add_root_tangent_driver(native,sketch['sketch'],low['id'])
    status=native.run('sketch.solve_status',sketch=sketch['sketch']);assert status['remaining_degrees_of_freedom']==0,status
    native.run('feature.set',container=sketch['owner'],parameters=dict(symmetric=True,profile_plane_offset=0))
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    native.run('save');native.run('close');native.run('open',path=str(target))
    ids=accept_existing(native,v,target);native.run('close');return ids


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--diameter',type=float)
    p.add_argument('--source',type=Path,default=ROOT/'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/05-NUTS/09-WING/01-PRESSED');a=p.parse_args()
    stage=a.stage.resolve();assert stage.is_relative_to(ROOT/'build') and not stage.exists();stage.mkdir(parents=True)
    source=a.source.resolve();assert source.is_dir()
    completed=[]
    for v in variants():
        if a.diameter is not None and v['diameter_mm']!=a.diameter:continue
        evidence=stage/'evidence'/Path(v['filename']).stem
        target=stage/v['filename'];original=source/v['filename'];assert original.is_file()
        if v['diameter_mm']==10:shutil.copy2(original,target)
        native=caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:
            if v['diameter_mm']==10:
                ids=stabilize_reference(native,v,target)
            else:ids=remodel(native,v,original,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8');completed.append(v)
        (stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored pressed wing candidate '+v['filename'],flush=True)


if __name__=='__main__':main()
