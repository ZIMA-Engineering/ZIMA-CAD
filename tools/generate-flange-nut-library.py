"""Author smooth-seat flange nuts using existing native modeling operations."""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('wheel',ROOT/'tools/generate-wheel-fastener-library.py')
wheel=importlib.util.module_from_spec(spec);spec.loader.exec_module(wheel)
caps=wheel.caps


def variants():
    facts=json.loads((ROOT/'tools/data/flange-nut-reviewed-dimensions.json').read_text(encoding='utf-8'));result=[]
    for row in facts['rows']:
        v=dict(zip(facts['columns'],row));d=v['diameter_mm']
        v.update(filename=f'FLN-DIN6923-M{d:04.1f}.prtz',standard='DIN 6923',family='10-FLANGE',
            roof_angle_degrees=40,chamfer_degrees=15,lip_radius_mm=v['flange_edge_height_mm']/5,
            bearing_radius_mm=(v['flange_diameter_mm']-v['minimum_bearing_diameter_mm'])/4,
            density_kg_mm3=7.85e-6,material='Steel',source_url=facts['source_url'],representation=facts['scope'],
            names=dict(zip(caps.LANGUAGES,('Šestihranná matice s přírubou, bez ozubení','Hexagon flange nut, no serrations',
                'Sechskant-Flanschmutter ohne Verzahnung','Écrou hexagonal à embase, sans denture','Шестигранная фланцевая гайка, без насечек'))))
        assert v['height_mm']-v['flange_edge_height_mm']-(v['flange_diameter_mm']-v['across_flats_mm'])/2*math.tan(math.radians(v['roof_angle_degrees']))>=v['minimum_wrench_height_mm']
        result.append(v)
    return result


def flange(native,v):
    R=v['flange_diameter_mm']/2;ri=v['diameter_mm']/2;c=v['flange_edge_height_mm']
    h=c+(R-ri)*math.tan(math.radians(v['roof_angle_degrees']))
    sk=native.run('sketch.create',name='Flange meridian',plane='XZ')['sketch']
    p=[[0,0],[R,0],[R,-c],[ri,-h],[0,-h]]
    lines=[native.run('sketch.segment.create',sketch=sk,first=q,second=p[(i+1)%5],snap_mm=1e-8)['geometry'] for i,q in enumerate(p)]
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity'];pts=[entity(line)['first'] for line in lines]
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[pts[0],'sketch_origin'])
    for i in (0,3):native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[lines[i]])
    for i in (1,4):native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[lines[i]])
    drivers={}
    for key,point,kind,axis,value in [('outside',pts[1],'distance_x','y',R),('inner',pts[3],'distance_x','y',ri),
        ('edge_height',pts[2],'distance_y','x',-c),('roof_height',pts[3],'distance_y','x',-h)]:
        drivers[key]=native.run('sketch.dimension.create',sketch=sk,kind=kind,points=[point],geometry=['sketch_axis:'+axis],value=value)['dimension']
    for key,first,second,radius in [('bearing_round',0,1,v['bearing_radius_mm']),('lip_round',1,2,v['lip_radius_mm'])]:
        corner=native.run('sketch.corner_fillet.create',sketch=sk,first=lines[first],second=lines[second],radius_mm=radius)['geometry']
        drivers[key]=native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[corner],value=radius)['dimension']
    axis=wheel.origin_axis(native,sk);status=native.run('sketch.solve_status',sketch=sk)
    assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
    turn=caps.side(operation='revolution');turn.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,parameters=dict(axis_segment_id=axis,symmetric=False,sides=[turn,caps.side(operation='none')]))['container']
    return dict(sketch=sk,feature=feature,**drivers)


def independent_volume(v):
    s,m,R,c,f,b=[v[k] for k in ('across_flats_mm','height_mm','flange_diameter_mm','flange_edge_height_mm','lip_radius_mm','bearing_radius_mm')];R/=2
    theta=math.radians(v['roof_angle_degrees']);slope=math.tan(theta);t=f*(1-math.sin(theta))/math.cos(theta)
    start=c-t;end=start+f*math.cos(theta);inner=v['diameter_mm']/2;upper=c+(R-inner)*slope
    r=s/math.sqrt(3);a=s/2;poly=[(r*math.cos(i*math.pi/3),r*math.sin(i*math.pi/3)) for i in range(6)]
    def radius(z):
        outer=R if z<=start else R-f+math.sqrt(max(0,f*f-(z-start)**2)) if z<end else R-(z-c)/slope
        if z<b:outer=min(outer,R-b+math.sqrt(max(0,b*b-(z-b)**2)))
        return outer
    knots=sorted({0.,b,start,end,upper,c+(R-r)*slope,c+(R-a)*slope})
    assert all(0<=z<=upper for z in knots)
    extra=0.
    for lo,hi in zip(knots,knots[1:]):
        mid=(lo+hi)/2;span=(hi-lo)/2
        def area(z):
            ro=radius(z);return math.pi*ro*ro-wheel.sections.disk_polygon(poly,ro)
        extra+=wheel.sections.integral(lambda q:area(mid-span*math.cos(q))*span*math.sin(q),0,math.pi,1e-7)
    bevel=(r-a)*math.tan(math.radians(v['chamfer_degrees']))
    top=wheel.sections.integral(lambda z:wheel.sections.disk_polygon(poly,a+(m-z)/math.tan(math.radians(v['chamfer_degrees']))),m-bevel,m,1e-7)
    rb=v['bore_mm']/2;mouth=v['mouth_chamfer_mm']
    return extra+math.sqrt(3)*s*s*(m-bevel)/2+top-math.pi*rb*rb*m-2*math.pi*(rb*mouth*mouth+mouth**3/3)


def author(native,v,target):
    native.run('new',type='part',name=target.stem);ids=dict(flange=flange(native,v))
    ids['blank']=wheel.weld.hexagon(native,v,0,v['height_mm'])
    ids['bevel']=wheel.weld.nuts.bevel(native,v,ids['blank']['feature'],True)
    thread=next(t for t in native.run('thread.catalog',standard='metric',limit=1000)['items'] if
        abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and abs(t['pitch_mm']-v['pitch_mm'])<1e-8)
    v['bore_mm']=thread['internal_root_diameter_mm'];v['mouth_chamfer_mm']=(v['diameter_mm']-v['bore_mm'])/2
    ids['thread']=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='through_all',thread_end='length',
        bore_length_mm=v['height_mm'],thread_length_mm=v['height_mm'],runout_pitch_factor=0,
        chamfer_enabled=True,chamfer_depth_mm=v['mouth_chamfer_mm'],chamfer_angle_degrees=90,drill_point_enabled=False,placement=dict(z=0))['container']
    edge=wheel.knurled.circular_edge(native,v['height_mm'],v['bore_mm']/2)
    ids['mouth']=native.run('chamfer.create',mode='equal_distance',distance_a_mm=v['mouth_chamfer_mm'],routes=[dict(edges=[edge])])['container']
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),dict(key='MASS_DENSITY',
        value=str(v['density_kg_mm3']),unit='kg/mm^3',descriptions=dict(zip(caps.LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    parameters=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},stock={'':thread['designation']},material={'':v['material']},quantity={'':'-'})
    for p in parameters:
        if p['key'] in values:p['values']=values[p['key']]
    native.run('document.parameters.set',parameters=parameters);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('document.relations.set',relations='mass = model.mass\n');native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    measured=native.run('body_properties.create')['volume_mm3'];native.run('undo');v['independent_volume_mm3']=independent_volume(v)
    if abs(measured-v['independent_volume_mm3'])>=max(1e-5,measured*1e-8):
        probe=target.with_name(target.stem+'-VOLUME-PROBE.prtz');native.run('save_as',path=str(probe))
        (target.parent/'volume-probe-manifest.json').write_text(json.dumps(dict(variants=[dict(v,filename=probe.name)])),encoding='utf-8')
    assert abs(measured-v['independent_volume_mm3'])<max(1e-5,measured*1e-8),(measured,v)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8');entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    symbol=lambda owner,key:'d'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    text='mass = model.mass\nstock = "M" & '+symbol(ids['thread'],'parameter:thread_designation')+' & "x" & '+symbol(ids['thread'],'parameter:pitch')+'\n'
    height=symbol(ids['blank']['feature'],'parameter:side0_length')
    for key in ('parameter:thread_length','parameter:bore_length'):text+=symbol(ids['thread'],key)+' = '+height+'\n'
    text+=symbol(ids['bevel']['sketch'],'dimension:'+ids['bevel']['width'])+' = '+symbol(ids['blank']['sketch'],'dimension:'+ids['blank']['size'])+' * (1 - sqrt(3) / 2) / 2\n'
    f=ids['flange'];dim=lambda key:symbol(f['sketch'],'dimension:'+f[key])
    text+=dim('roof_height')+' = '+dim('edge_height')+' - ('+dim('outside')+' - '+dim('inner')+f') * {math.tan(math.radians(v["roof_angle_degrees"])):.17g}\n'
    native.run('document.relations.set',relations=text);native.run('regenerate');assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')
    return ids


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',type=Path,required=True);args=parser.parse_args()
    stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);completed=[]
    for v in variants():
        target=stage/v['filename'];assert not target.exists();evidence=stage/'evidence'/target.stem
        native=caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=author(native,v,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8');completed.append(v)
        (stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)
