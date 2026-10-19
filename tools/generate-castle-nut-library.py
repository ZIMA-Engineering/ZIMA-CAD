"""Author historical castle-nut candidates; never install unverified candidates.

Slot-root finishing and current-standard equivalence require separate review.
The archived CSN dimensions are not relabelled as a current ISO standard.
"""
import argparse
import configparser
import hashlib
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('nuts',ROOT/'tools/generate-hex-nut-library.py')
nuts=importlib.util.module_from_spec(spec);spec.loader.exec_module(nuts)
spec=importlib.util.spec_from_file_location('sections',ROOT/'tools/castle-nut-volume.py')
sections=importlib.util.module_from_spec(spec);spec.loader.exec_module(sections)


def variants():
    source=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))
    result={}
    number=lambda v:float(v.strip('() ').replace(',','.'))
    for topic,standard in [('01-CSN-1411','021411'),('02-LOW','021412')]:
        for table in source['05-NUTS/02-CASTLE/'+topic]['tables']:
            for header in table[:2]:
                for col,title in enumerate(header[2:],2):
                    if table[6][col]=='–':continue
                    parts=title.strip('()').replace('×','x')[1:].split('x')
                    d=number(parts[0]);pitch=number(parts[1]) if len(parts)>1 else None
                    key=(standard,d,pitch)
                    names=('Korunová matice','Castle nut','Kronenmutter','Écrou à créneaux','Корончатая гайка')
                    if standard=='021412':names=('Nízká korunová matice','Thin castle nut','Niedrige Kronenmutter','Écrou bas à créneaux','Низкая корончатая гайка')
                    result[key]=dict(filename=f'CN-CSN{standard}-M{d:04.1f}'+(f'x{pitch:04.2f}' if pitch else '')+'.prtz',
                        standard='ČSN 02 '+standard[2:],family='02-CASTLE/'+topic,diameter_mm=d,pitch_mm=pitch,
                        across_flats_mm=number(table[2][col]),height_mm=number(table[7][col]),
                        slot_width_mm=number(table[5][col]),slot_floor_mm=number(table[6][col]),
                        crown_diameter_mm=None if table[4][col]=='–' else number(table[4][col]),
                        chamfer_degrees=30,names=dict(zip(nuts.caps.LANGUAGES,names)),
                        density_kg_mm3=7.85e-6,material='Steel',fine_pitch=pitch is not None,
                        review_status='candidate: current-standard equivalence and crown finishing review pending')
    return list(result.values())


def crown(native,v,target,ids):
    if v['crown_diameter_mm'] is None:return
    native.run('open',path=str(target))
    r=v['across_flats_mm']/math.sqrt(3);m=v['height_mm'];h=v['slot_floor_mm']-v.get('crown_relief_mm',0);radius=v['crown_diameter_mm']/2
    assert v['diameter_mm']/2<radius<r and 0<h<m
    sk=native.run('sketch.create',name='Cylindrical crown turning',plane='XZ')['sketch']
    curves=native.run('sketch.rectangle.create',sketch=sk,first=[radius,-m],second=[r,-h])['geometry']
    entity=lambda g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
    points={p for g in curves for p in (entity(g)['first'],entity(g)['second'])}
    corner=next(p for p in points if abs(entity(p)['x']-r)<1e-8 and abs(entity(p)['y']+m)<1e-8)
    guide=native.run('sketch.segment.create',sketch=sk,first=[r,0],second=[r,-m],construction=True)['geometry']
    source=nuts.caps.socket_profile.source_point(native,ids['prism'],r,0)
    ref=nuts.caps.socket_profile.attach(native,sk,source)
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(guide)['first'],ref])
    assert entity(guide)['second']==corner
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[guide])
    guide_length=native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[guide],value=m)['dimension']
    inner=next(p for p in points if abs(entity(p)['x']-radius)<1e-8 and abs(entity(p)['y']+m)<1e-8)
    width=native.run('sketch.dimension.create',sketch=sk,kind='distance_x',points=[inner],geometry=['sketch_axis:y'],value=radius)['dimension']
    height=native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[curves[1]],value=m-h)['dimension']
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,0],second=[0,1],construction=True)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(axis)['first'],'sketch_origin'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[axis],value=1)
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    rotation=nuts.caps.side(operation='revolution');rotation.update(rotation_extent='full',angle_degrees=360)
    cut=native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(axis_segment_id=axis,symmetric=False,
        sides=[rotation,nuts.caps.side(operation='none')]))['container']
    native.run('regenerate');assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')
    ids['crown']=dict(sketch=sk,radius=width,height=height,guide_length=guide_length,feature=cut)


def slots(native,v,target,ids):
    native.run('open',path=str(target));cuts=[]
    for angle in (0,60,120):
        sk=native.run('sketch.create',name='Crown slot profile '+str(angle),plane='XZ')['sketch']
        radius=v['slot_width_mm']/2;cy=-v['slot_floor_mm']-radius;top=-v['height_mm']-1
        left=native.run('sketch.segment.create',sketch=sk,first=[-radius,top],second=[-radius,cy])['geometry']
        root=native.run('sketch.arc.create',sketch=sk,center=[0,cy],start=[-radius,cy],end=[radius,cy],clockwise=True,snap_mm=1e-8)['geometry']
        right=native.run('sketch.segment.create',sketch=sk,first=[radius,cy],second=[radius,top])['geometry']
        cap=native.run('sketch.segment.create',sketch=sk,first=[radius,top],second=[-radius,top])['geometry']
        entity=lambda g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
        for g in (left,right):native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[g])
        native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[cap])
        for a,b in ((left,root),(root,right)):
            native.run('sketch.constraint.create',sketch=sk,kind='tangent',geometry=[a,b])
        center=entity(root)['center']
        native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[center],geometry=['sketch_axis:y'])
        breadth=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[root],value=2*radius)['dimension']
        bottom=native.run('sketch.point.create',sketch=sk,position=[0,-v['slot_floor_mm']],construction=True,snap_mm=1e-8)['point']
        native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[bottom],geometry=['sketch_axis:y'])
        native.run('sketch.constraint.create',sketch=sk,kind='point_on_circle',points=[bottom],geometry=[root])
        y=native.run('sketch.dimension.create',sketch=sk,kind='distance_y',points=[bottom],geometry=['sketch_axis:x'],value=-v['slot_floor_mm'])['dimension']
        length=native.run('sketch.dimension.create',sketch=sk,kind='distance_y',points=[entity(left)['first']],geometry=['sketch_axis:x'],value=top)['dimension']
        assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
        cut=native.run('feature.create',sketch=sk,combine='subtract',placement=dict(rotation_z=angle),
            parameters=dict(symmetric=True,
                sides=[nuts.caps.side(v['across_flats_mm'],extent='through_all'),nuts.caps.side(v['across_flats_mm'],extent='through_all')]))['container']
        cuts.append(dict(sketch=sk,y=y,top=length,width=breadth,root=root,feature=cut))
    native.run('regenerate');assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8')
    entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    def symbol(owner,key):return 'd'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    native.run('open',path=str(target))
    relations=native.run('document.relations.get')['relations']
    first_width=symbol(cuts[0]['sketch'],'dimension:'+cuts[0]['width'])
    first_floor=symbol(cuts[0]['sketch'],'dimension:'+cuts[0]['y'])
    floor='(-'+first_floor+')'
    for index,c in enumerate(cuts):
        dim=lambda key:symbol(c['sketch'],'dimension:'+c[key])
        relations+='\n'+dim('top')+' = -'+symbol(ids['prism'],'parameter:side0_length')+' - 1\n'
        if index:
            relations+=f'{dim("width")} = {first_width}\n'
            relations+=dim('y')+' = '+first_floor+'\n'
    if 'crown' in ids:
        relations+=symbol(ids['crown']['sketch'],'dimension:'+ids['crown']['height'])+' = '+symbol(ids['prism'],'parameter:side0_length')+' - '+floor+' + '+str(v.get('crown_relief_mm',0))+'\n'
        relations+=symbol(ids['crown']['sketch'],'dimension:'+ids['crown']['guide_length'])+' = '+symbol(ids['prism'],'parameter:side0_length')+'\n'
        if v.get('crown_diameter_from_flats'):
            relations+=symbol(ids['crown']['sketch'],'dimension:'+ids['crown']['radius'])+' = '+symbol(ids['sketch'],'dimension:'+ids['size'])+' * sqrt(3) / 4\n'
    native.run('document.relations.set',relations=relations)
    native.run('regenerate');assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')
    ids['slots']=cuts;return ids


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--filename')
    p.add_argument('--resume',action='store_true')
    args=p.parse_args();stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True)
    manifest=stage/'manifest.json'
    completed=json.loads(manifest.read_text(encoding='utf-8'))['variants'] if manifest.exists() else []
    for v in variants():
        if args.filename and v['filename']!=args.filename:continue
        target=stage/v['filename'];evidence=stage/'evidence'/target.stem
        if target.exists():
            saved=next((r for r in completed if r['filename']==target.name),None)
            assert args.resume and saved and saved['generated_sha256']==hashlib.sha256(target.read_bytes()).hexdigest(),target
            continue
        native=nuts.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:
            ids=nuts.author(native,v,target);crown(native,v,target,ids);ids=slots(native,v,target,ids)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        v['independent_volume_mm3']=sections.volume(v)
        v['generated_sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
        completed.append(v)
        manifest.write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored and reopened castle-nut candidate '+target.name,flush=True)


if __name__=='__main__':main()
