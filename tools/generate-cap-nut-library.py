"""Author nominal CSN 02 1431:1970 closed nuts from reviewed primary dimensions.

The closed end is a 45-degree cone ending in a flat of diameter k. The
120-degree callout belongs to the thread-mouth countersink. DIN 1587 uses
its separate generator; the retired spherical CSN studies are not installed.
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
spec=importlib.util.spec_from_file_location('sections',ROOT/'tools/castle-nut-volume.py')
sections=importlib.util.module_from_spec(spec);spec.loader.exec_module(sections)


def variants():
    review=json.loads((ROOT/'tools/data/csn-closed-nut-reviewed-dimensions.json').read_text(encoding='utf-8'))
    result=[]
    for row in review['rows']:
        d=row['diameter_mm']
        result.append(dict(row,filename=f'CN-CSN021431-M{d:04.1f}.prtz',
            standard='ČSN 02 1431',family='04-CAP/01-CSN021431',pitch_mm=None,
            closed_profile='cone_flat',crown_angle_degrees=45,mouth_angle_degrees=120,chamfer_degrees=30,
            material='Steel',density_kg_mm3=7.85e-6,source_url=review['source_url'],
            names=dict(zip(caps.LANGUAGES,('Uzavřená matice','Closed nut','Geschlossene Mutter','Écrou borgne','Закрытая гайка'))),
            representation='Nominal dimensions from CSN 02 1431:1970; schematic thread, no complete manufacturing certification'))
    return result


def independent_volume(v):
    s,h,k,p,rb,c=[v[key] for key in ('across_flats_mm','height_mm','flat_diameter_mm','bore_depth_mm','bore_mm','mouth_chamfer_mm')]
    rb/=2;r=s/math.sqrt(3);a=s/2
    assert 0<k/2<a and p>v['thread_length_mm'] and p<h
    assert rb<k/2,'Blind bore would break through the cone'
    polygon=[(r*math.cos(i*math.pi/3),r*math.sin(i*math.pi/3)) for i in range(6)]
    area=math.sqrt(3)*s*s/2
    start=h-(r-k/2)/math.tan(math.radians(v['crown_angle_degrees']))
    bevel=(r-a)*math.tan(math.radians(v['chamfer_degrees']))
    assert start>bevel
    top=sections.integral(lambda z:sections.disk_polygon(polygon,k/2+(h-z)*math.tan(math.radians(v['crown_angle_degrees']))),start,h,1e-9)
    bottom=sections.integral(lambda z:sections.disk_polygon(polygon,a+z/math.tan(math.radians(v['chamfer_degrees']))),0,bevel,1e-9)
    slope=math.tan(math.radians(v['mouth_angle_degrees']/2))
    return bottom+area*(start-bevel)+top-math.pi*rb*rb*p-math.pi*(rb*slope*c*c+slope*slope*c**3/3)


def crown(native,v,prism):
    h,k=v['height_mm'],v['flat_diameter_mm'];r=v['across_flats_mm']/math.sqrt(3)
    sk=native.run('sketch.create',name='Closed end meridian',plane='XZ')['sketch']
    entity=lambda g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
    a,b,c=[k/2,-h],[r,-h],[r,-h+(r-k/2)/math.tan(math.radians(v['crown_angle_degrees']))]
    slope=native.run('sketch.segment.create',sketch=sk,first=a,second=c,snap_mm=1e-8)['geometry']
    rim=native.run('sketch.segment.create',sketch=sk,first=c,second=b,snap_mm=1e-8)['geometry']
    top=native.run('sketch.segment.create',sketch=sk,first=b,second=a,snap_mm=1e-8)['geometry']
    B,A=entity(top)['first'],entity(top)['second']
    ref=caps.socket_profile.attach(native,sk,caps.socket_profile.source_point(native,prism,r,h))
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[B,ref])
    native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[top])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[rim])
    flat=native.run('sketch.dimension.create',sketch=sk,kind='distance_x',points=[A],geometry=['sketch_axis:y'],value=k/2)['dimension']
    angle=native.run('sketch.dimension.create',sketch=sk,kind='angle_between',geometry=['sketch_axis:x',slope],value=v['crown_angle_degrees'])['dimension']
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,.123456789],second=[0,1.123456789],construction=True)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(axis)['first'],'sketch_origin'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[axis],value=1)
    solved=native.run('sketch.solve_status',sketch=sk)
    assert solved['remaining_degrees_of_freedom']==0 and solved['maximum_residual']<1e-8,solved
    turn=caps.side(operation='revolution');turn.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(axis_segment_id=axis,symmetric=False,sides=[turn,caps.side(operation='none')]))['container']
    return dict(sketch=sk,flat=flat,angle=angle,feature=feature)


def author(native,v,target):
    catalog=native.run('thread.catalog',standard='metric',limit=1000)['items']
    thread=next(t for t in catalog if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and
        (abs(t['pitch_mm']-v['pitch_mm'])<1e-8 if v['pitch_mm'] else 'x' not in t['designation']))
    v.update(pitch_mm=thread['pitch_mm'],bore_mm=thread['internal_root_diameter_mm'])
    v['mouth_chamfer_mm']=(v['diameter_mm']-v['bore_mm'])/2/math.tan(math.radians(v['mouth_angle_degrees']/2))
    v['independent_volume_mm3']=independent_volume(v)
    native.run('new',type='part',name=target.stem)
    sk=native.run('sketch.create',name='Hexagon profile',plane='XY')['sketch'];r=v['across_flats_mm']/math.sqrt(3)
    native.run('sketch.polygon.create',sketch=sk,center=[0,0],rim=[r,0],sides=6)
    entity=lambda g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
    items=native.run('sketch.entities',sketch=sk)['items'];circle=next(e for e in items if e['kind']=='circle')
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(circle['id'])['center'],'sketch_origin'])
    size=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle['id']],value=2*r)['dimension']
    vertex=next(e['id'] for e in items if e['kind']=='point' and abs(entity(e['id'])['y'])<1e-8 and entity(e['id'])['x']>0)
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[vertex],geometry=['sketch_axis:x'])
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    prism=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,sides=[caps.side(v['height_mm']),caps.side(operation='none')]))['container']
    closed_end=crown(native,v,prism);bevel=nuts.bevel(native,v,prism,False)
    hole=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='length',thread_end='length',
        bore_length_mm=v['bore_depth_mm'],thread_length_mm=v['thread_length_mm'],runout_pitch_factor=0,
        chamfer_enabled=True,chamfer_depth_mm=v['mouth_chamfer_mm'],chamfer_angle_degrees=v['mouth_angle_degrees'],
        drill_point_enabled=False,placement=dict(z=0))['container']
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),
        dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',
            descriptions=dict(zip(caps.LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    params=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},stock={'':thread['designation']},material={'':v['material']},quantity={'':'-'})
    for p in params:
        if p['key'] in values:p['values']=values[p['key']]
    native.run('document.parameters.set',parameters=params);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('document.relations.set',relations='mass = model.mass\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors']
    measured=native.run('body_properties.create')['volume_mm3'];native.run('undo')
    assert abs(measured-v['independent_volume_mm3'])<max(1e-6,measured*1e-8),(measured,v)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8')
    entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    symbol=lambda owner,key:'d'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    text=f'mass = model.mass\nstock = "M" & {symbol(hole,"parameter:thread_designation")} & "x" & {symbol(hole,"parameter:pitch")}\n'
    text+=symbol(bevel['sketch'],'dimension:'+bevel['width'])+' = '+symbol(sk,'dimension:'+size)+' * (1 - sqrt(3) / 2) / 2\n'
    native.run('document.relations.set',relations=text);native.run('regenerate')
    assert not native.run('history.list')['calculation_errors']
    native.run('save');native.run('close')
    return dict(sketch=sk,size=size,prism=prism,crown=closed_end,bevel=bevel,thread=hole)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--filename');args=parser.parse_args()
    stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);completed=[]
    for v in variants():
        if args.filename and args.filename!=v['filename']:continue
        target=stage/v['filename'];assert not target.exists()
        evidence=stage/'evidence'/target.stem
        native=caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=author(native,v,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        completed.append(v);(stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored blind candidate '+target.name,flush=True)


if __name__=='__main__':main()
