"""Stage compressed spring washers through native, editable solid operations.

Unresolved source rows are reported and excluded rather than silently repaired.
The split is a documented representative CAD gap, not a manufacturing gauge.
"""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('special',ROOT/'tools/generate-special-cap-screw-library.py')
special=importlib.util.module_from_spec(spec);spec.loader.exec_module(special)
caps=special.caps
LANGUAGES=caps.LANGUAGES


def number(text):
    match=re.match(r'\(?\s*([0-9]+(?:[,.][0-9]+)?)',text)
    if not match:raise ValueError(text)
    return float(match[1].replace(',','.'))


def variants():
    table=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))['06-WASHERS/03-SPRING']['tables'][0]
    reviews=json.loads((ROOT/'tools/data/spring-washer-reviewed-dimensions.json').read_text(encoding='utf-8'))
    result=[];unresolved=[]
    for row in table[2:]:
        d=number(row[0].lstrip('M'));bore=number(row[1])
        for standard,outer_col,width_col,thickness_col,height_col,section in (
                ('021740',3,4,4,5,'square'),('021741',7,8,9,10,'rectangular')):
            if row[outer_col].strip() in ('–','—','-',''):continue
            corrections=[c for c in reviews['corrections'] if c['standard']==standard and c['nominal_mm']==d]
            def nominal(column):
                correction=next((c for c in corrections if c['column']==column),None)
                if correction:
                    assert row[column]==correction['old_cell'],'Reviewed source cell changed'
                    return correction['nominal_mm_corrected']
                return number(row[column])
            outer=nominal(outer_col);width=nominal(width_col);thickness=nominal(thickness_col)
            if abs(outer-bore-2*width)>1e-7:
                unresolved.append(dict(standard='ČSN 02 '+standard[2:],nominal_mm=d,source_row=row,
                    reason='Outside diameter does not equal bore plus twice the radial width'))
                continue
            names=dict(zip(LANGUAGES,(
                'Pružná podložka, stlačený stav',
                'Spring washer, compressed state',
                'Federring, zusammengedrückter Zustand',
                'Rondelle élastique, état comprimé',
                'Пружинная шайба, сжатое состояние')))
            result.append(dict(filename=f'SW-CSN{standard}-M{d:04.1f}.prtz',standard='ČSN 02 '+standard[2:],
                nominal_mm=d,bore_mm=bore,outside_mm=outer,width_mm=width,thickness_mm=thickness,
                free_height_mm=number(row[height_col]),section=section,corner_radius_mm=number(row[2]),
                state='compressed',split_angle_degrees=45,split_gap_mm=1,split_geometry='parallel axial cut',
                names=names,preferred=not row[1].startswith('('),density_kg_mm3=7.85e-6,
                source_corrections=corrections))
    return result,unresolved


def relations(native,path,identities):
    data=configparser.RawConfigParser();data.read(path,encoding='utf-8')
    entries=json.loads(data['Document']['dimension_identifiers'])['entries']
    def symbol(owner,key):return 'd'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    outside,bore=[symbol(identities['sketch'],'dimension:'+d) for d in identities['diameters']]
    thickness=symbol(identities['annulus'],'parameter:side0_length')
    native.run('document.relations.set',relations=f'stock = "Ø" & {bore} & "x" & {outside} & "x" & {thickness}\nmass = model.mass\n')


def split_profile(native,v,annulus):
    """Axial 45-degree parallel cut linked to the actual two end planes."""
    sk=native.run('sketch.create',name='Split profile',plane='XZ')['sketch']
    references=[]
    feature=native.run('feature.get',container=annulus)['feature']
    for z in (0,v['thickness_mm']):
        candidates=[r for r,surface in caps.studs.faces(native,annulus) if surface and surface['kind']=='plane'
            and abs(surface['origin'][2]-z)<1e-7 and ':'+feature+':end:' in r['key']]
        assert len(candidates)==1,('Washer end plane missing',z)
        references.append(native.run('sketch.reference.create',sketch=sk,kind='face',**caps.studs.ref(candidates[0]))['reference'])
    t=v['thickness_mm'];half=v['split_gap_mm']/math.sqrt(2)
    # XZ uses local +Y towards world -Z. Center the oblique opening across
    # the thickness so its starting plane stays inside the bore at all sizes.
    points=[[t/2-half,0],[-t/2-half,-t],[-t/2+half,-t],[t/2+half,0]]
    segments=[native.run('sketch.segment.create',sketch=sk,first=p,second=points[(i+1)%4])['geometry'] for i,p in enumerate(points)]
    entities=[native.run('sketch.entity.get',sketch=sk,entity=g)['entity'] for g in segments]
    for point in (entities[0]['first'],entities[3]['first']):
        native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[point],geometry=[references[0]])
    for point in (entities[1]['first'],entities[2]['first']):
        native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[point],geometry=[references[1]])
    native.run('sketch.constraint.create',sketch=sk,kind='parallel',geometry=[segments[0],segments[2]])
    native.run('sketch.dimension.create',sketch=sk,kind='angle_between',geometry=['sketch_axis:x',segments[2]],value=45)
    native.run('sketch.dimension.create',sketch=sk,kind='line_distance',geometry=[segments[0],segments[2]],value=v['split_gap_mm'])
    diagonal=native.run('sketch.segment.create',sketch=sk,first=points[0],second=points[2],construction=True)['geometry']
    native.run('sketch.constraint.create',sketch=sk,kind='midpoint_on_line',geometry=[diagonal,'sketch_axis:y'])
    status=native.run('sketch.solve_status',sketch=sk)
    assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
    return sk,references


def author(native,v,path):
    native.run('new',type='part',name=path.stem)
    sk=native.run('sketch.create',name='Annular profile',plane='XY')['sketch'];circles=[];diameters=[];centers=set()
    for diameter in (v['outside_mm'],v['bore_mm']):
        circle=native.run('sketch.circle.create',sketch=sk,center=[0,0],radius_mm=diameter/2)['geometry']
        center=native.run('sketch.entity.get',sketch=sk,entity=circle)['entity']['center']
        if center not in centers:
            native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[center,'sketch_origin'])
            centers.add(center)
        diameters.append(native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle],value=diameter)['dimension'])
        circles.append(circle)
    annulus=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,
        sides=[caps.side(v['thickness_mm']),caps.side(operation='none')]))['container']
    # Round the section before the split so the authored R belongs to the wire,
    # without rounding the freshly cut ends or moving either seating plane.
    edges=[caps.circular_edge(native,annulus,z,r) for z in (0,v['thickness_mm'])
        for r in (v['bore_mm']/2,v['outside_mm']/2)]
    rounded=native.run('fillet.create',radius_mm=v['corner_radius_mm'],routes=[dict(edges=[e]) for e in edges])['container']
    split_sk,split_references=split_profile(native,v,annulus)
    split=native.run('feature.create',sketch=split_sk,combine='subtract',parameters=dict(symmetric=False,
        sides=[caps.side(v['thickness_mm'],extent='through_all'),caps.side(operation='none')]))['container']
    descriptions=dict(zip(LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность')))
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value='ČSN 12 061'),
        dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',descriptions=descriptions)])
    params=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},material={'':'ČSN 12 061'},
        stock={'':f'M{v["nominal_mm"]:g}'},quantity={'':'-'})
    for p in params:
        if p['key'] in values:p['values']=values[p['key']]
    native.run('document.parameters.set',parameters=params)
    native.run('document.relations.set',relations='mass = model.mass\n')
    native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    for sketch in (sk,split_sk):
        status=native.run('sketch.solve_status',sketch=sketch)
        assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
    native.run('save_as',path=str(path));native.run('close',discard=True);native.run('open',path=str(path))
    native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    identities=dict(sketch=sk,circles=circles,diameters=diameters,annulus=annulus,rounded=rounded,split_sketch=split_sk,split=split,split_references=split_references)
    relations(native,path,identities);native.run('regenerate');native.run('save');native.run('close')
    return identities


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--filename');parser.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    parser.add_argument('--refresh-relations',action='store_true')
    args=parser.parse_args();stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True)
    rows,unresolved=variants()
    (stage/'manifest.json').write_text(json.dumps(dict(variants=rows,unresolved=unresolved),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    for v in rows:
        if args.filename and v['filename']!=args.filename:continue
        target=stage/v['filename'];evidence=stage/'evidence'/target.stem
        if target.exists():
            assert (evidence/'identities.json').is_file(),target
            if args.refresh_relations:
                identities=json.loads((evidence/'identities.json').read_text(encoding='utf-8'))
                native=caps.studs.api.Native(args.cli.resolve(),stage/'relations-evidence'/target.stem)
                try:
                    native.run('open',path=str(target));relations(native,target,identities)
                    native.run('regenerate');native.run('save');native.run('close')
                finally:native.close()
            continue
        native=caps.studs.api.Native(args.cli.resolve(),evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:identities=author(native,v,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(identities,indent=2)+'\n',encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)
    print(f'{len(rows)} candidate definitions; {len(unresolved)} unresolved source rows',flush=True)


if __name__=='__main__':main()
