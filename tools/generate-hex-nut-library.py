"""Author Origin-seated hexagonal nuts with native threaded openings.

All candidates remain outside the factory library pending independent checks.
"""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('caps',ROOT/'tools/generate-cap-screw-library.py')
caps=importlib.util.module_from_spec(spec);spec.loader.exec_module(caps)


def variants():
    source=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))
    reviewed=json.loads((ROOT/'tools/data/nut-reviewed-dimensions.json').read_text(encoding='utf-8'))
    grade=source[reviewed['source_table'].split('#')[0]]['tables'][0]
    for c in reviewed['corrections']:
        assert grade[c['row']+1][c['column']]==c['before']
        grade[c['row']+1][c['column']]=c['after']
    result={};number=lambda text:float(text.strip('() ').replace(',','.'))
    standards=['4032','8673','4035','8675','4036','4033','8674','4034']
    names=('Šestihranná matice','Hexagon nut','Sechskantmutter','Écrou hexagonal','Шестигранная гайка')
    for topic,standard in zip(sorted(k for k in source if k.startswith('05-NUTS/01-HEXAGON/')),standards):
        for table in source[topic]['tables']:
            if not table or not re.match(r'^\(?M\d',table[0][1]):continue
            labels=[re.sub(r'[\s_.]','',r[0]).lower() for r in table]
            def row(prefix):return next(i for i,text in enumerate(labels) if text.startswith(prefix))
            height=row('mmax');flats=row('smax') if standard!='4034' else row('s[')
            headers=[r for r in table[:2] if re.match(r'^\(?M\d',r[1])]
            for header in headers:
                for col,title in enumerate(header[1:],1):
                    text=title.strip('()').replace('×','x');parts=text[1:].split('x')
                    d=number(parts[0]);pitch=number(parts[1]) if len(parts)==2 else (number(table[1][col]) if standard!='4034' else None)
                    s,m=number(table[flats][col]),number(table[height][col])
                    assert s>d and m>0,(topic,title,s,m)
                    fine=len(parts)==2;key=(standard,d,pitch)
                    v=dict(filename=f'HN-ISO{standard}-M{d:04.1f}'+(f'x{pitch:04.2f}' if fine else '')+'.prtz',
                        standard='ISO '+standard,family=topic.removeprefix('05-NUTS/'),diameter_mm=d,pitch_mm=pitch,
                        across_flats_mm=s,height_mm=m,chamfer_degrees=0 if standard=='4036' else 30,
                        names=dict(zip(caps.LANGUAGES,names)),density_kg_mm3=7.85e-6,material='Steel',fine_pitch=fine)
                    if key in result:assert result[key]==v
                    result[key]=v
    return list(result.values())


def bevel(native,v,prism,top):
    s,m=v['across_flats_mm'],v['height_mm'];a=s/2;r=s/math.sqrt(3);dx=r-a;z=m if top else 0
    h=dx*math.tan(math.radians(v['chamfer_degrees']))
    sk=native.run('sketch.create',name='Upper chamfer profile' if top else 'Seating chamfer profile',plane='XZ')['sketch']
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,0],second=[0,1],construction=True)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    entity=lambda g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(axis)['first'],'sketch_origin'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[axis],value=1)
    points=[[a,-z],[r,-z],[r,-z+(h if top else -h)]]
    segments=[native.run('sketch.segment.create',sketch=sk,first=p,second=points[(i+1)%3])['geometry'] for i,p in enumerate(points)]
    rim=caps.socket_profile.source_point(native,prism,r,z)
    ref=caps.socket_profile.attach(native,sk,rim)
    edge=entity(segments[0]);native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[edge['second'],ref])
    native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[segments[0]])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[segments[1]])
    width=native.run('sketch.dimension.create',sketch=sk,kind='distance_x',points=[edge['first'],edge['second']],value=dx)['dimension']
    native.run('sketch.dimension.create',sketch=sk,kind='angle_between',geometry=[segments[0],segments[2]],value=180-v['chamfer_degrees'])
    solved=native.run('sketch.solve_status',sketch=sk)
    assert solved['remaining_degrees_of_freedom']==0 and solved['maximum_residual']<1e-8,solved
    actual=entity(entity(segments[1])['second']);assert abs(actual['y']-points[2][1])<1e-8,(top,actual,points)
    side=caps.side(operation='revolution');side.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(axis_segment_id=axis,symmetric=False,sides=[side,caps.side(operation='none')]))['container']
    return dict(sketch=sk,width=width,feature=feature)


def author(native,v,target):
    catalog=native.run('thread.catalog',standard='metric',limit=1000)['items']
    thread=next(t for t in catalog if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and
        (abs(t['pitch_mm']-v['pitch_mm'])<1e-8 if v['pitch_mm'] else 'x' not in t['designation']))
    v['pitch_mm']=thread['pitch_mm'];v['bore_mm']=thread['internal_root_diameter_mm']
    native.run('new',type='part',name=target.stem)
    sk=native.run('sketch.create',name='Hexagon profile',plane='XY')['sketch']
    native.run('sketch.polygon.create',sketch=sk,center=[0,0],rim=[v['across_flats_mm']/math.sqrt(3),0],sides=6)
    items=native.run('sketch.entities',sketch=sk)['items']
    circle=next(e for e in items if e['kind']=='circle');entity=lambda g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(circle['id'])['center'],'sketch_origin'])
    size=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle['id']],value=2*v['across_flats_mm']/math.sqrt(3))['dimension']
    vertex=next(e['id'] for e in items if e['kind']=='point' and abs(entity(e['id'])['y'])<1e-8 and entity(e['id'])['x']>0)
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[vertex],geometry=['sketch_axis:x'])
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    prism=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,sides=[caps.side(v['height_mm']),caps.side(operation='none')]))['container']
    bevels=[bevel(native,v,prism,top) for top in (False,True)] if v['chamfer_degrees'] else []
    c=(v['diameter_mm']-v['bore_mm'])/2;v['mouth_chamfer_mm']=c
    assert c>0 and 2*c<v['height_mm'],v
    hole=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='through_all',thread_end='length',
        bore_length_mm=v['height_mm'],thread_length_mm=v['height_mm'],runout_pitch_factor=0,
        chamfer_enabled=True,chamfer_depth_mm=c,chamfer_angle_degrees=90,drill_point_enabled=False,placement=dict(z=0))['container']
    rim=caps.circular_edge(native,hole,v['height_mm'],v['bore_mm']/2)
    mouth=native.run('chamfer.create',mode='equal_distance',distance_a_mm=c,routes=[dict(edges=[rim])])['container']
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',
        descriptions=dict(zip(caps.LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    params=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},stock={'':thread['designation']},material={'':v['material']},quantity={'':'-'})
    for p in params:
        if p['key'] in values:p['values']=values[p['key']]
    native.run('document.parameters.set',parameters=params);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('document.relations.set',relations='mass = model.mass\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors']
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    ids=dict(sketch=sk,size=size,prism=prism,bevels=bevels,thread=hole,mouth=mouth)
    relations(native,target,ids)
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    native.run('save');native.run('close')
    return ids


def relations(native,target,ids):
    sk,size,prism,hole,bevels=[ids[k] for k in ('sketch','size','prism','thread','bevels')]
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8')
    entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    def symbol(owner,key):return 'd'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    diameter=symbol(hole,'parameter:thread_designation');pitch=symbol(hole,'parameter:pitch');profile=symbol(sk,'dimension:'+size)
    height=symbol(prism,'parameter:side0_length')
    text=f'stock = "M" & {diameter} & "x" & {pitch}\nmass = model.mass\n'
    text+=symbol(hole,'parameter:thread_length')+' = '+height+'\n'
    text+=symbol(hole,'parameter:bore_length')+' = '+height+'\n'
    for b in bevels:text+=symbol(b['sketch'],'dimension:'+b['width'])+' = '+profile+' * (1 - sqrt(3) / 2) / 2\n'
    native.run('document.relations.set',relations=text)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--filename')
    p.add_argument('--refresh-relations',action='store_true')
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe');args=p.parse_args()
    stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True)
    rows=json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants'] if args.refresh_relations else variants()
    for v in rows:
        if args.filename and args.filename!=v['filename']:continue
        path=stage/v['filename'];assert path.exists() if args.refresh_relations else not path.exists(),path
        evidence=stage/'evidence'/path.stem
        native=caps.studs.api.Native(args.cli.resolve(),evidence,settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:
            if args.refresh_relations:
                ids=json.loads((evidence/'identities.json').read_text(encoding='utf-8'))
                native.run('open',path=str(path));relations(native,path,ids);native.run('regenerate')
                assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')
            else:ids=author(native,v,path)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        (stage/'manifest.json').write_text(json.dumps(dict(variants=[r for r in rows if (stage/r['filename']).exists()]),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('Authored and reopened '+path.name,flush=True)


if __name__=='__main__':main()
