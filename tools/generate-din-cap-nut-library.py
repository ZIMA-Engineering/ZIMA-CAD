"""Author a separate DIN 1587 family; never relabel historical CSN models.

Dimensional baseline: Aspen Fasteners DIN 1587 specification, cross-checked
against the Böllhoff product drawing and original DIN dimensional drawing.
Threads use the application's nominal bore and schematic thread representation.
"""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('historical',ROOT/'tools/generate-cap-nut-library.py')
historical=importlib.util.module_from_spec(spec);spec.loader.exec_module(historical)
nuts=historical.nuts;caps=historical.caps;sections=historical.sections


def variants():
    # Nominal dimensions, not a reproduction of manufacturing tolerances.
    diameters=[4,5,6,8,10,12,14,16,18,20,22,24]
    flats=[7,8,10,13,16,18,21,24,27,30,34,36]
    heights=[8,10,12,15,18,22,25,28,32,34,39,42]
    hex_heights=[3.2,4,5,6.5,8,10,11,13,15,16,18,19]
    radii=[3.25,3.75,4.75,6.25,7.5,8.5,10,11.5,13,14,16.5,17]
    thread_depth=[5.5,7.5,8,11,13,16,18,21,25,26,29,31]
    wall=[2,2,2,2,2,3,4,4,5,5,5,6]
    coarse=[.7,.8,1,1.25,1.5,1.75,2,2,2.5,2.5,2.5,3]
    fine={8:[1],10:[1,1.25],12:[1.5,1.25],14:[1.5],16:[1.5],
        18:[1.5,2],20:[2,1.5],22:[1.5,2],24:[2]}
    rows=[]
    for i,d in enumerate(diameters):
        for pitch in [coarse[i]]+fine.get(d,[]):
            is_fine=pitch!=coarse[i]
            rows.append(dict(filename=f'CN-DIN1587-M{d:04.1f}'+(f'x{pitch:04.2f}' if is_fine else '')+'.prtz',
                standard='DIN 1587',family='04-CAP/02-DIN1587',diameter_mm=d,pitch_mm=pitch,
                across_flats_mm=flats[i],height_mm=heights[i],hex_height_mm=hex_heights[i],
                dome_radius_mm=radii[i],thread_length_mm=thread_depth[i],
                minimum_pole_wall_mm=wall[i],chamfer_degrees=30,
                runout_pitch_factor=.5,material='Steel',density_kg_mm3=7.85e-6,
                fine_pitch=is_fine,names=dict(zip(caps.LANGUAGES,('Uzavřená matice','Cap nut','Hutmutter','Écrou borgne','Колпачковая гайка'))),
                source_url='https://www.aspenfasteners.com/content/pdf/Metric_DIN_1587_spec.pdf',
                representation='Nominal CAD geometry; finite schematic thread with selected 0.5P run-out, no manufacturing certification'))
    for v in rows:
        v['bore_depth_mm']=resolved_bore_depth(v)
        v['pole_wall_mm']=v['height_mm']-v['bore_depth_mm']
    assert len(rows)==26
    return rows


def resolved_bore_depth(v):
    # Leave one pitch beyond the usable thread where space permits. The
    # tabulated minimum pole wall limits depth; it is not a drilling target.
    return min(v['height_mm']-v['minimum_pole_wall_mm'],v['thread_length_mm']+v['pitch_mm'])


def independent_volume(v):
    s,m,R,h,p,rb,c=[v[k] for k in ('across_flats_mm','hex_height_mm','dome_radius_mm','height_mm','bore_depth_mm','bore_mm','mouth_chamfer_mm')]
    rb/=2;a=s/2;corner=s/math.sqrt(3)
    assert h-R>=m and p>v['thread_length_mm']
    assert R-math.hypot(rb,p-h+R)>0,'DIN blind bore breaks through the crown'
    polygon=[(corner*math.cos(i*math.pi/3),corner*math.sin(i*math.pi/3)) for i in range(6)]
    area=math.sqrt(3)*s*s/2;bevel=(corner-a)*math.tan(math.radians(v['chamfer_degrees']))
    bevel_volume=sections.integral(lambda z:sections.disk_polygon(polygon,a+z/math.tan(math.radians(v['chamfer_degrees']))),0,bevel,1e-9)
    # Radius edits may grow the cap slightly beyond the hexagon's inscribed
    # circle. Integrate the true disk/hexagon overlap rather than assuming the
    # entire cylinder remains inside the hexagon below m.
    transition=max(0.,min(bevel,(R-a)*math.tan(math.radians(v['chamfer_degrees']))))
    intersection=sections.disk_polygon(polygon,R)
    overlap=intersection*(m-2*transition)
    if transition>0:
        overlap+=2*sections.integral(lambda z:sections.disk_polygon(polygon,a+z/math.tan(math.radians(v['chamfer_degrees']))),0,transition,1e-9)
    outer=2*bevel_volume+area*(m-2*bevel)+math.pi*R*R*(h-R)+2*math.pi*R**3/3-overlap
    return outer-math.pi*rb*rb*p-math.pi*(rb*c*c+c**3/3)


def crown(native,v):
    R,h=v['dome_radius_mm'],v['height_mm']
    sk=native.run('sketch.create',name='DIN cap meridian',plane='XZ')['sketch']
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    base=native.run('sketch.segment.create',sketch=sk,first=[0,0],second=[R,0])['geometry']
    side=native.run('sketch.segment.create',sketch=sk,first=[R,0],second=[R,-h+R])['geometry']
    arc=native.run('sketch.arc.create',sketch=sk,center=[0,-h+R],start=[R,-h+R],end=[0,-h],clockwise=True)['geometry']
    close=native.run('sketch.segment.create',sketch=sk,first=[0,-h],second=[0,0])['geometry']
    pole=native.run('sketch.segment.create',sketch=sk,first=[0,-h],second=[1,-h],construction=True)['geometry']
    origin=entity(base)['first']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[origin,'sketch_origin'])
    for key,kind in [(base,'horizontal'),(side,'vertical'),(close,'vertical'),(pole,'horizontal')]:
        native.run('sketch.constraint.create',sketch=sk,kind=kind,geometry=[key])
    for key in (side,pole):native.run('sketch.constraint.create',sketch=sk,kind='tangent',geometry=[key,arc])
    radius=native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[arc],value=R)['dimension']
    height=native.run('sketch.dimension.create',sketch=sk,kind='distance_y',points=[entity(close)['first'],origin],value=h)['dimension']
    native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[pole],value=1)
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,.123456789],second=[0,1.123456789],construction=True)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(axis)['first'],'sketch_origin'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[axis],value=1)
    solved=native.run('sketch.solve_status',sketch=sk)
    assert solved['remaining_degrees_of_freedom']==0 and solved['maximum_residual']<1e-8,solved
    turn=caps.side(operation='revolution');turn.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,parameters=dict(axis_segment_id=axis,symmetric=False,
        sides=[turn,caps.side(operation='none')]))['container']
    return dict(sketch=sk,feature=feature,radius=radius,height=height)


def author(native,v,target):
    thread=next(t for t in native.run('thread.catalog',standard='metric',limit=1000)['items']
        if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and abs(t['pitch_mm']-v['pitch_mm'])<1e-8)
    v['bore_mm']=thread['internal_root_diameter_mm'];v['mouth_chamfer_mm']=(v['diameter_mm']-v['bore_mm'])/2
    v['independent_volume_mm3']=independent_volume(v)
    v['minimum_spherical_wall_mm']=v['dome_radius_mm']-math.hypot(v['bore_mm']/2,v['bore_depth_mm']-v['height_mm']+v['dome_radius_mm'])
    native.run('new',type='part',name=target.stem)
    sk=native.run('sketch.create',name='Hexagon profile',plane='XY')['sketch'];r=v['across_flats_mm']/math.sqrt(3)
    native.run('sketch.polygon.create',sketch=sk,center=[0,0],rim=[r,0],sides=6)
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    items=native.run('sketch.entities',sketch=sk)['items'];circle=next(e for e in items if e['kind']=='circle')
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(circle['id'])['center'],'sketch_origin'])
    size=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle['id']],value=2*r)['dimension']
    vertex=next(e['id'] for e in items if e['kind']=='point' and entity(e['id'])['x']>0 and abs(entity(e['id'])['y'])<1e-8)
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[vertex],geometry=['sketch_axis:x'])
    prism=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,sides=[caps.side(v['hex_height_mm']),caps.side(operation='none')]))['container']
    hex_dimensions=dict(v,height_mm=v['hex_height_mm'])
    bevels=[nuts.bevel(native,hex_dimensions,prism,top) for top in (False,True)]
    dome=crown(native,v)
    hole=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='length',thread_end='length',
        bore_length_mm=v['bore_depth_mm'],thread_length_mm=v['thread_length_mm'],runout_pitch_factor=v['runout_pitch_factor'],
        chamfer_enabled=True,chamfer_depth_mm=v['mouth_chamfer_mm'],chamfer_angle_degrees=90,drill_point_enabled=False,placement=dict(z=0))['container']
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
    assert abs(measured-v['independent_volume_mm3'])<max(1e-6,measured*1e-8),(measured,v)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8')
    entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    symbol=lambda owner,key:'d'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    relations=f'mass = model.mass\nstock = "M" & {symbol(hole,"parameter:thread_designation")} & "x" & {symbol(hole,"parameter:pitch")}\n'
    relations+=symbol(hole,'parameter:bore_length')+' = min('+symbol(dome['sketch'],'dimension:'+dome['height'])+f' - {v["minimum_pole_wall_mm"]:g}, '+symbol(hole,'parameter:thread_length')+' + '+symbol(hole,'parameter:pitch')+')\n'
    for bevel in bevels:relations+=symbol(bevel['sketch'],'dimension:'+bevel['width'])+' = '+symbol(sk,'dimension:'+size)+' * (1 - sqrt(3) / 2) / 2\n'
    native.run('document.relations.set',relations=relations);native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    native.run('save');native.run('close')
    return dict(sketch=sk,size=size,prism=prism,bevels=bevels,crown=dome,thread=hole)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--filename');args=parser.parse_args();stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);completed=[]
    for v in variants():
        if args.filename and v['filename']!=args.filename:continue
        target=stage/v['filename'];assert not target.exists()
        evidence=stage/'evidence'/target.stem
        native=caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=author(native,v,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        completed.append(v);(stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored separate DIN cap nut '+target.name,flush=True)


if __name__=='__main__':main()
