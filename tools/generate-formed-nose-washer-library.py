"""Author historical nose washers with an actual native Flat/Bend/Flat history."""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tab',ROOT/'tools/generate-tab-washer-library.py')
tab=importlib.util.module_from_spec(spec);spec.loader.exec_module(tab)
caps=tab.caps


def variants():
    data=json.loads((ROOT/'tools/data/formed-nose-washer-reviewed-dimensions.json').read_text(encoding='utf-8'))
    for row in data['rows']:
        v=dict(zip(data['fields'],row));v.update(filename=f'NTW-CSN021753-M{v["nominal_mm"]:04.1f}.prtz',
            standard=data['standard'],family='05-LOCK/02-FORMED-NOSE',density_kg_mm3=7.85e-6,material='ČSN 11 321.21',
            names=dict(zip(caps.LANGUAGES,('Pojistná podložka s nosem','External tab washer','Sicherungsblech mit Nase',
                'Rondelle-frein à ergot','Стопорная шайба с носиком'))))
        v['leg_length_mm']=v['height_mm']-2*v['thickness_mm'];v['angle_degrees']=90
        assert v['leg_length_mm']>0
        yield v


def independent_volume(v):
    R=v['outside_mm']/2;c=v['root_distance_mm'];t=v['thickness_mm'];f=v['nose_width_mm']
    assert v['bore_mm']/2<c<R and f/2<math.sqrt(R*R-c*c)
    area=R*R*(math.pi-math.acos(c/R))+c*math.sqrt(R*R-c*c)-math.pi*(v['bore_mm']/2)**2
    return area*t+f*math.radians(v['angle_degrees'])*(1.5*t)*t+f*v['leg_length_mm']*t


def bounds(v):
    R=v['outside_mm']/2;t=v['thickness_mm'];angle=math.radians(v['angle_degrees'])
    assert 0<angle<=math.pi/2
    height=2*t*(1-math.cos(angle))+v['leg_length_mm']*math.sin(angle)+t*math.cos(angle)
    right=v['root_distance_mm']+2*t*math.sin(angle)+v['leg_length_mm']*math.cos(angle)
    return [-R,-R,t-height,right,R,t]


def attachment_edge(native,axis,value,z,width):
    matches=[]
    for edge in native.run('edge_treatment.edges',limit=5000)['items']:
        points=[p['position_mm'] for segment in edge['segments'] for p in segment['endpoints']]
        if len(points)==2 and all(abs(p[axis]-value)<1e-6 and abs(abs(p[1])-width/2)<1e-6 and abs(p[2]-z)<1e-6 for p in points):
            matches.append(edge)
    assert len(matches)==1,('Nose sheet attachment is ambiguous',value,z,len(matches))
    return caps.studs.ref(matches[0])


def author(native,v,target):
    native.run('new',type='part',name=target.stem)
    flat=native.run('flat.create',width_mm=10,height_mm=10,thickness_mm=v['thickness_mm'],name='Nose washer base')
    sk=flat['sketch']
    for point in native.run('sketch.entities',sketch=sk)['items']:
        if point['kind']=='point':
            current={e['id'] for e in native.run('sketch.entities',sketch=sk)['items']}
            if point['id'] in current:native.run('sketch.point.delete',sketch=sk,point=point['id'])
    R=v['outside_mm']/2;f=v['nose_width_mm'];c=v['root_distance_mm'];d=v['bore_mm'];t=v['thickness_mm']
    y=math.sqrt(R*R-c*c)
    arc=native.run('sketch.arc.create',sketch=sk,center=[0,0],start=[c,y],end=[c,-y],clockwise=False)['geometry']
    segments=[];points=[[c,-y],[c,-f/2],[c,f/2],[c,y]]
    for a,b in zip(points,points[1:]):segments.append(native.run('sketch.segment.create',sketch=sk,first=a,second=b,snap_mm=1e-8)['geometry'])
    bore=native.run('sketch.circle.create',sketch=sk,center=[0,0],radius_mm=d/2)['geometry']
    entity=lambda g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(bore)['center'],'sketch_origin'])
    for line in segments:native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[line])
    native.run('sketch.constraint.create',sketch=sk,kind='midpoint_on_line',geometry=[segments[1],'sketch_axis:x'])
    dims={}
    for key,kind,g,value in [('outside','radius',arc,R),('bore','diameter',bore,d),('nose_width','distance',segments[1],f)]:
        dims[key]=native.run('sketch.dimension.create',sketch=sk,kind=kind,geometry=[g],value=value)['dimension']
    dims['root']=native.run('sketch.dimension.create',sketch=sk,kind='distance_x',points=[entity(segments[1])['first']],geometry=['sketch_axis:y'],value=c)['dimension']
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    ref=attachment_edge(native,0,c,t,f)
    bend=native.run('bend.create',width_mm=f,edge_owner=ref['owner'],edge_key=ref['key'],angle_degrees=90,
        radius_follows_thickness=True,k_factor=.5,name='Formed nose bend')
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    ref=attachment_edge(native,0,c+2*t,-t,f)
    terminal=native.run('flat.create',width_mm=f,height_mm=v['leg_length_mm'],edge_owner=ref['owner'],edge_key=ref['key'],name='Nose straight leg')
    leg=terminal['sketch'];lines=[e['id'] for e in native.run('sketch.entities',sketch=leg)['items'] if e['kind']=='segment']
    first=native.run('sketch.entity.get',sketch=leg,entity=lines[0])['entity']['first']
    native.run('sketch.constraint.create',sketch=leg,kind='point_reference',points=[first,'sketch_origin'])
    leg_dims={}
    for key,line,value in [('width',lines[0],f),('length',lines[1],v['leg_length_mm'])]:
        leg_dims[key]=native.run('sketch.dimension.create',sketch=leg,kind='distance',geometry=[line],value=value)['dimension']
    assert native.run('sketch.solve_status',sketch=leg)['remaining_degrees_of_freedom']==0
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',
        descriptions=dict(zip(caps.LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    rows=native.run('document.parameters.get')['parameters'];values=dict(name=v['names'],standard={'':v['standard']},material={'':v['material']},quantity={'':'-'})
    for row in rows:
        if row['key'] in values:row['values']=values[row['key']]
    native.run('document.parameters.set',parameters=rows);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('document.relations.set',relations='mass = model.mass\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors']
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8');entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    def symbol(owner,key):return 'd'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    width=symbol(sk,'dimension:'+dims['nose_width']);length=symbol(leg,'dimension:'+leg_dims['length'])
    thickness=symbol(flat['container'],'parameter:thickness')
    # Relations exposes angular dimensions in its base unit, radians.
    angle=symbol(bend['container'],'parameter:angle')
    height=f'(2 * {thickness} * (1 - cos({angle})) + {length} * sin({angle}) + max(0, {thickness} * cos({angle})))'
    relations=[f'{symbol(leg,"dimension:"+leg_dims["width"])} = {width}',
        f'stock = "Ø" & {symbol(sk,"dimension:"+dims["bore"])} & "x" & (2 * {symbol(sk,"dimension:"+dims["outside"])}) & "x" & {thickness} & ";f=" & {width} & ";l=" & {symbol(sk,"dimension:"+dims["root"])} & ";h=" & {height}',
        'mass = model.mass']
    native.run('document.relations.set',relations='\n'.join(relations)+'\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors']
    stock=next(p['values'][''] for p in native.run('document.parameters.get')['parameters'] if p['key']=='stock')
    assert abs(float(stock.split(';h=')[1])-v['height_mm'])<1e-5,stock
    properties=native.run('body_properties.create');native.run('undo')
    v['independent_volume_mm3']=independent_volume(v)
    v['bounds_mm']=bounds(v)
    assert abs(properties['volume_mm3']-v['independent_volume_mm3'])<max(1e-7,v['independent_volume_mm3']*1e-8),(properties,v)
    native.run('save');native.run('close')
    return dict(base=flat,base_dimensions=dims,bend=bend,leg=terminal,leg_dimensions=leg_dims)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--nominal',type=float);a=p.parse_args()
    stage=a.stage.resolve();assert stage.is_relative_to(ROOT/'build') and not stage.exists();stage.mkdir(parents=True)
    rows=[]
    for v in variants():
        if a.nominal is not None and a.nominal!=v['nominal_mm']:continue
        evidence=stage/'evidence'/Path(v['filename']).stem
        native=caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=author(native,v,stage/v['filename'])
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8');rows.append(v)
        (stage/'manifest.json').write_text(json.dumps(dict(variants=rows),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored native formed nose '+v['filename'],flush=True)


if __name__=='__main__':main()
