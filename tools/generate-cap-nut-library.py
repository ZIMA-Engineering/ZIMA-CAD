"""Stage blind cap-nut candidates from historical tabulated dimensions.

The archived drawing does not dimension its spherical crown radius. These
uninstalled candidates explicitly select R=2*d and a 30-degree seating bevel.
They are not a claim of a fully specified manufacturing execution or DIN/ISO
interchangeability. Keep the original dimension table intact.
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
    table=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))['05-NUTS/04-CAP']['tables'][0]
    number=lambda text:float(text.strip('() ').replace(',','.'))
    result={}
    for header in table[:2]:
        for column,title in enumerate(header[1:],1):
            parts=title.strip('()').replace('×','x')[1:].split('x')
            d=number(parts[0]);pitch=number(parts[1]) if len(parts)>1 else None
            v=dict(filename=f'CN-CSN021431-M{d:04.1f}'+(f'x{pitch:04.2f}' if pitch else '')+'.prtz',
                standard='ČSN 02 1431',family='04-CAP',diameter_mm=d,pitch_mm=pitch,
                across_flats_mm=number(table[2][column]),height_mm=number(table[5][column]),
                thread_length_mm=number(table[4][column]),bore_depth_mm=number(table[7][column]),
                archived_k_mm=number(table[6][column]),dome_radius_mm=2*d,chamfer_degrees=30,
                material='Steel',density_kg_mm3=7.85e-6,
                names=dict(zip(caps.LANGUAGES,('Uzavřená matice','Cap nut','Hutmutter','Écrou borgne','Колпачковая гайка'))),
                review_status='uninstalled candidate; historical dimensions; selected CAD crown R=2*d and seating bevel 30 degrees')
            result[(d,pitch)]=v
    return list(result.values())


def independent_volume(v):
    s,h,R,p,rb,c=[v[k] for k in ('across_flats_mm','height_mm','dome_radius_mm','bore_depth_mm','bore_mm','mouth_chamfer_mm')]
    rb/=2;r=s/math.sqrt(3);a=s/2
    assert R>r and p>v['thread_length_mm'] and p<h
    assert (p-h+R)**2+rb*rb<R*R,'Blind bore would break through the crown'
    polygon=[(r*math.cos(i*math.pi/3),r*math.sin(i*math.pi/3)) for i in range(6)]
    area=math.sqrt(3)*s*s/2
    start=h-R+math.sqrt(R*R-r*r)
    bevel=(r-a)*math.tan(math.radians(v['chamfer_degrees']))
    assert start>bevel
    top=sections.integral(lambda z:sections.disk_polygon(polygon,math.sqrt(max(0,R*R-(z-h+R)**2))),start,h,1e-9)
    bottom=sections.integral(lambda z:sections.disk_polygon(polygon,a+z/math.tan(math.radians(v['chamfer_degrees']))),0,bevel,1e-9)
    return bottom+area*(start-bevel)+top-math.pi*rb*rb*p-math.pi*(rb*c*c+c**3/3)


def dome(native,v,prism):
    h,R=v['height_mm'],v['dome_radius_mm'];r=v['across_flats_mm']/math.sqrt(3)
    sag=R-math.sqrt(R*R-r*r)
    sk=native.run('sketch.create',name='Cap dome meridian',plane='XZ')['sketch']
    entity=lambda g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
    a,b,c=[0,-h],[r,-h],[r,-h+sag]
    top=native.run('sketch.segment.create',sketch=sk,first=a,second=b,snap_mm=1e-8)['geometry']
    rim=native.run('sketch.segment.create',sketch=sk,first=b,second=c,snap_mm=1e-8)['geometry']
    arc=native.run('sketch.arc.create',sketch=sk,center=[0,-h+R],start=c,end=a,clockwise=True,snap_mm=1e-8)['geometry']
    A,B=entity(top)['first'],entity(top)['second']
    ref=caps.socket_profile.attach(native,sk,caps.socket_profile.source_point(native,prism,r,h))
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[B,ref])
    native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[top])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[rim])
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[A],geometry=['sketch_axis:y'])
    native.run('sketch.constraint.create',sketch=sk,kind='tangent',geometry=[top,arc])
    radius=native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[arc],value=R)['dimension']
    # Do not merge the rotation axis with the arc centre merely because R=h
    # happens to put that independent centre at the Origin in one size.
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,.123456789],second=[0,1.123456789],construction=True)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(axis)['first'],'sketch_origin'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[axis],value=1)
    solved=native.run('sketch.solve_status',sketch=sk)
    assert solved['remaining_degrees_of_freedom']==0 and solved['maximum_residual']<1e-8,solved
    turn=caps.side(operation='revolution');turn.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(axis_segment_id=axis,symmetric=False,sides=[turn,caps.side(operation='none')]))['container']
    return dict(sketch=sk,radius=radius,feature=feature)


def author(native,v,target):
    catalog=native.run('thread.catalog',standard='metric',limit=1000)['items']
    thread=next(t for t in catalog if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and
        (abs(t['pitch_mm']-v['pitch_mm'])<1e-8 if v['pitch_mm'] else 'x' not in t['designation']))
    v.update(pitch_mm=thread['pitch_mm'],bore_mm=thread['internal_root_diameter_mm'])
    v['mouth_chamfer_mm']=(v['diameter_mm']-v['bore_mm'])/2
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
    crown=dome(native,v,prism);bevel=nuts.bevel(native,v,prism,False)
    hole=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='length',thread_end='length',
        bore_length_mm=v['bore_depth_mm'],thread_length_mm=v['thread_length_mm'],runout_pitch_factor=0,
        chamfer_enabled=True,chamfer_depth_mm=v['mouth_chamfer_mm'],chamfer_angle_degrees=90,
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
    return dict(sketch=sk,size=size,prism=prism,crown=crown,bevel=bevel,thread=hole)


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
