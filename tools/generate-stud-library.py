"""Author historical table studs from their metal-end seating plane.

Fine pitch applies only to the nut end. Nominal length excludes the metal end.
Candidates are staged; installation requires separate verification and artwork.
"""
import argparse
import configparser
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fasteners',ROOT/'tools/generate-fastener-library.py')
api=importlib.util.module_from_spec(spec);spec.loader.exec_module(api)
LANGUAGES=('cs','en','de','fr','ru')
COARSE={3:.5,4:.7,5:.8,6:1,8:1.25,10:1.5,12:1.75,14:2,16:2,18:2.5,20:2.5,22:2.5,24:3,27:3,30:3.5}


def variants():
    source=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))
    result=[];seen=set()
    number=lambda v:float(v.replace(',','.'))
    for group in ('01-M3-M16','02-M16-M30'):
        dimensions,masses=source['04-SCREWS/03-STUDS/'+group]['tables']
        for column,title in enumerate(dimensions[0][1:],1):
            d=number(title.strip('()')[1:])
            if d in seen:continue
            seen.add(d)
            pitches=[COARSE[d]]
            fine=dimensions[1][column].strip('()')
            if fine:pitches.append(number(fine.split('×')[1]))
            b=number(dimensions[2][column].split('|')[0].strip())
            lengths=[number(row[0]) for row in masses[3:] if row[0].isdigit() and row[column].strip() not in ('','—','-')]
            for standard,row,material in [('021174',3,'steel'),('021176',4,'cast iron'),('021178',5,'aluminium')]:
                if not dimensions[row][column]:continue
                e=number(dimensions[row][column])
                # Retain established representative lengths and avoid the
                # historical short-length exceptions.
                length=next(l for l in lengths if l>=min(50,max(lengths)) and (l+e)/2>=b+2*max(pitches)+1)
                for pitch in pitches:
                    fine_pitch=pitch!=COARSE[d]
                    size=f'M{int(d):02d}'+(f'x{pitch:.2f}' if fine_pitch else '')
                    names=dict(zip(LANGUAGES,(
                        'Závrtný šroub '+{'steel':'do oceli','cast iron':'do litiny','aluminium':'do hliníku'}[material],
                        'Stud for '+material,
                        'Stiftschraube für '+{'steel':'Stahl','cast iron':'Gusseisen','aluminium':'Aluminium'}[material],
                        'Goujon pour '+{'steel':'acier','cast iron':'fonte','aluminium':'aluminium'}[material],
                        'Шпилька для '+{'steel':'стали','cast iron':'чугуна','aluminium':'алюминия'}[material])))
                    result.append(dict(filename=f'STD-CSN{standard}-{size}x{int(length):03d}.prtz',
                        standard='ČSN 02 '+standard[2:],diameter_mm=d,pitch_mm=pitch,
                        metal_pitch_mm=COARSE[d],fine_pitch=fine_pitch,length_mm=length,
                        metal_length_mm=e,thread_length_mm=b,total_length_mm=length+e,
                        designation=f'M{d:g}'+(('×'+f'{pitch:g}'.replace('.',',')) if fine_pitch else ''),
                        metal_designation=f'M{d:g}',property_class='5.8',names=names,
                        preferred=not title.startswith('(')))
    assert len({v['filename'] for v in result})==len(result)
    return result


def ref(v):return {key:v[key] for key in ('owner','key')}


def faces(native,owner=None):
    args=dict(kind='face')
    if owner:args['owner']=owner
    return [(r,native.run('reference.get',kind='face',**ref(r),limit=1).get('surface'))
        for r in native.run('reference.list',**args)['items'] if not r['key'].startswith('origin:')]


def relations(native,v,path,identity):
    data=configparser.RawConfigParser();data.read(path,encoding='utf-8')
    entries=json.loads(data['Document']['dimension_identifiers'])['entries']
    def symbol(owner,key):return 'd'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    diameter=symbol(identity['sketch'],'dimension:'+identity['diameter']['dimension'])
    length=symbol(identity['shaft'],'parameter:side1_length')
    pitch=f'x{v["pitch_mm"]:g}' if v['fine_pitch'] else ''
    native.run('document.relations.set',relations=f'stock = "M" & {diameter} & "{pitch}x" & {length}\nmass = model.mass\n')


def author(native,v,path):
    native.run('new',type='part',name=path.stem)
    sk=native.run('sketch.create',name='Profile',plane='XY')['sketch']
    circle=native.run('sketch.circle.create',sketch=sk,center=[0,0],radius_mm=v['diameter_mm']/2)['geometry']
    center=native.run('sketch.entity.get',sketch=sk,entity=circle)['entity']['center']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[center,'sketch_origin'])
    diameter=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle],value=v['diameter_mm'])
    side=dict(operation='extrude',length=v['length_mm'],extrusion_extent='length',angle_degrees=90,
        rotation_extent='angle',draft_angle_degrees=0,targets=[])
    metal_side={**side,'length':v['metal_length_mm']}
    shaft=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,sides=[metal_side,side]))['container']
    ends=[]
    for r in native.run('reference.list',kind='edge',owner=shaft)['items']:
        g=native.run('reference.get',kind='edge',**ref(r),limit=1)
        if any(abs(g['segments'][0]['points'][0][2]-z)<1e-6 for z in (v['metal_length_mm'],-v['length_mm'])):ends.append(ref(r))
    assert len(ends)==2
    chamfer=native.run('chamfer.create',mode='equal_distance',distance_a_mm=.5*v['metal_pitch_mm'],
        routes=[dict(edges=[r]) for r in ends])['container']
    source=faces(native,shaft);cones=faces(native,chamfer);threads=[]
    for sign,designation,length,pitch in ((1,v['metal_designation'],v['metal_length_mm'],v['metal_pitch_mm']),
                                         (-1,v['designation'],v['thread_length_mm'],v['pitch_mm'])):
        cylinder=next(r for r,s in source if s and s['kind']=='cylinder' and s['axis'][2]*sign<0)
        end=v['metal_length_mm'] if sign>0 else -v['length_mm']
        start=next(r for r,s in source if s and s['kind']=='plane' and abs(s['origin'][2]-end)<.001)
        cone=next(r for r,s in cones if s and s['kind']=='cone' and s['origin'][2]*sign>0)
        thread=native.run('shaft_thread.create',cylinder=ref(cylinder),start=ref(start),chamfer=ref(cone),
            designation=designation,length_mm=length,end_condition='length',runout_enabled=True,runout_pitch_factor=2)
        threads.append(thread['container'])
    native.run('document.material.load',path=str(ROOT/'config/materials/01_steels/fasteners/Steel-5.8.matz'))
    params=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},stock={'':v['designation'].replace('×','x')+f'x{v["length_mm"]:g}'},
        material={'':'5.8'},quantity={'':'-'})
    for p in params:
        if p['key'] in values:p['values']=values[p['key']]
    native.run('document.parameters.set',parameters=params)
    native.run('document.relations.set',relations='mass = model.mass\n')
    native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    native.run('save_as',path=str(path));native.run('close',discard=True);native.run('open',path=str(path))
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    for container,length in zip(threads,(v['metal_length_mm'],v['thread_length_mm'])):
        thread=native.run('shaft_thread.get',container=container)
        assert abs(thread['length_mm']-length)<1e-8
    identities=dict(shaft=shaft,sketch=sk,circle=circle,center=center,diameter=diameter,chamfer=chamfer,threads=threads)
    relations(native,v,path,identities)
    native.run('save');native.run('close')
    return identities


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe');p.add_argument('--filename')
    p.add_argument('--refresh-relations',action='store_true')
    args=p.parse_args();stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True)
    rows=variants();(stage/'manifest.json').write_text(json.dumps(dict(variants=rows),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    for v in rows:
        if args.filename and v['filename']!=args.filename:continue
        path=stage/v['filename']
        if path.exists():
            if not (stage/'evidence'/path.stem/'identities.json').is_file():raise RuntimeError('Unverified existing candidate '+str(path))
            if args.refresh_relations:
                identity=json.loads((stage/'evidence'/path.stem/'identities.json').read_text(encoding='utf-8'))
                native=api.Native(args.cli.resolve(),stage/'relation-evidence'/path.stem)
                try:
                    native.run('open',path=str(path));relations(native,v,path,identity);native.run('regenerate');native.run('save');native.run('close')
                finally:native.close()
            continue
        native=api.Native(args.cli.resolve(),stage/'evidence'/path.stem,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:receipt=author(native,v,path)
        finally:native.close()
        (stage/'evidence'/path.stem/'identities.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
        print('Authored and reopened '+path.name,flush=True)


if __name__=='__main__':main()
