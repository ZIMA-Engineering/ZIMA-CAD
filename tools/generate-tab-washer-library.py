"""Author flat CSN 02 1751 tab washers with a tangent, editable outline."""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('washers',ROOT/'tools/generate-spring-washer-library.py')
washers=importlib.util.module_from_spec(spec);spec.loader.exec_module(washers)
caps=washers.caps


def variants():
    data=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))
    result=[]
    for row in data['06-WASHERS/05-LOCK']['tables'][0][2:]:
        d=washers.number(row[0].lstrip('M'));bore,outside,b,l,r,t=[washers.number(row[i]) for i in (1,2,3,4,5,6)]
        result.append(dict(filename=f'TW-CSN021751-M{d:04.1f}.prtz',standard='ČSN 02 1751',family='05-LOCK',
            nominal_mm=d,bore_mm=bore,outside_mm=outside,tab_width_mm=b,tab_length_mm=l,root_radius_mm=r,thickness_mm=t,
            width_mm=(outside-bore)/2,density_kg_mm3=7.85e-6,material='ČSN 11 321.21',
            names=dict(zip(caps.LANGUAGES,('Pojistná podložka s jazýčkem','Tab washer','Sicherungsblech mit Lappen','Rondelle-frein à languette','Стопорная шайба с лапкой'))),
            bounds_mm=[-l,outside/2,-outside/2,outside/2,0,t]))
    return result


def author(native,v,target):
    native.run('new',type='part',name=target.stem)
    sk=native.run('sketch.create',name='Tab washer profile',plane='XY')['sketch']
    radius=v['outside_mm']/2;f=v['root_radius_mm'];b=v['tab_width_mm'];l=v['tab_length_mm']
    cy=b/2+f;cx=-math.sqrt((radius+f)**2-cy**2);px=cx*radius/(radius+f);py=cy*radius/(radius+f)
    assert -l<cx<px<0 and py>b/2,v
    top=native.run('sketch.segment.create',sketch=sk,first=[-l,b/2],second=[cx,b/2])['geometry']
    upper=native.run('sketch.arc.create',sketch=sk,center=[cx,cy],start=[cx,b/2],end=[px,py],clockwise=False,snap_mm=1e-8)['geometry']
    outer=native.run('sketch.arc.create',sketch=sk,center=[0,0],start=[px,py],end=[px,-py],clockwise=True,snap_mm=1e-8)['geometry']
    lower=native.run('sketch.arc.create',sketch=sk,center=[cx,-cy],start=[px,-py],end=[cx,-b/2],clockwise=False,snap_mm=1e-8)['geometry']
    bottom=native.run('sketch.segment.create',sketch=sk,first=[cx,-b/2],second=[-l,-b/2])['geometry']
    end=native.run('sketch.segment.create',sketch=sk,first=[-l,-b/2],second=[-l,b/2])['geometry']
    bore=native.run('sketch.circle.create',sketch=sk,center=[0,0],radius_mm=v['bore_mm']/2)['geometry']
    entity=lambda g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
    center=entity(bore)['center'];assert entity(outer)['center']==center
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[center,'sketch_origin'])
    for g in (top,bottom):native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[g])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[end])
    for a,c in ((top,upper),(upper,outer),(outer,lower),(lower,bottom)):
        native.run('sketch.constraint.create',sketch=sk,kind='tangent',geometry=[a,c])
    native.run('sketch.constraint.create',sketch=sk,kind='equal_radius',geometry=[upper,lower])
    centers=[entity(entity(g)['center']) for g in (upper,lower)]
    symmetry=native.run('sketch.segment.create',sketch=sk,first=[centers[0]['x'],centers[0]['y']],second=[centers[1]['x'],centers[1]['y']],construction=True)['geometry']
    native.run('sketch.constraint.create',sketch=sk,kind='midpoint_on_line',geometry=[symmetry,'sketch_axis:x'])
    dims={}
    for key,g,value in (('outside',outer,radius),('upper_root',upper,f)):
        dims[key]=native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[g],value=value)['dimension']
    dims['bore']=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[bore],value=v['bore_mm'])['dimension']
    dims['width']=native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[end],value=b)['dimension']
    dims['length']=native.run('sketch.dimension.create',sketch=sk,kind='distance_x',points=[entity(top)['first']],geometry=['sketch_axis:y'],value=-l)['dimension']
    status=native.run('sketch.solve_status',sketch=sk)
    assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
    body=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,sides=[caps.side(v['thickness_mm']),caps.side(operation='none')]))['container']
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',
        descriptions=dict(zip(caps.LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    parameters=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},stock={'':f'M{v["nominal_mm"]:g}'},material={'':v['material']},quantity={'':'-'})
    for row in parameters:
        if row['key'] in values:row['values']=values[row['key']]
    native.run('document.parameters.set',parameters=parameters);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('document.relations.set',relations='mass = model.mass\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors'];native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8');entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    def symbol(owner,key):return 'd'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    symbols={key:symbol(sk,'dimension:'+dim) for key,dim in dims.items()};thickness=symbol(body,'parameter:side0_length')
    text=f'stock = "Ø" & {symbols["bore"]} & "x" & (2 * {symbols["outside"]}) & "x" & {thickness} & ";b=" & {symbols["width"]} & ";l=" & (-{symbols["length"]})\n'
    text+='mass = model.mass\n'
    native.run('document.relations.set',relations=text);native.run('regenerate');assert not native.run('history.list')['calculation_errors'];native.run('save');native.run('close')
    return dict(sketch=sk,dimensions=dims,body=body,outer=outer,bore=bore)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--filename')
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe');args=p.parse_args()
    stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);rows=variants()
    for v in rows:
        if args.filename and v['filename']!=args.filename:continue
        target=stage/v['filename'];assert not target.exists();evidence=stage/'evidence'/target.stem
        native=caps.studs.api.Native(args.cli.resolve(),evidence,settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=author(native,v,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        (stage/'manifest.json').write_text(json.dumps(dict(variants=[r for r in rows if (stage/r['filename']).exists()]),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)


if __name__=='__main__':main()
