"""Author fitted-bolt candidates from the approved native user template.

Dimensions come from the repository's historical fitted-bolt tables. They are
not a claim of complete conformance to the replacement 2003 standards. Native
commands own geometry, references, identifiers, persistence and derived mass.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fasteners', ROOT/'tools/generate-fastener-library.py')
native_tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native_tools)
SHAFT='01a14429e3207acba34fc9c038cdcc7f'
HEAD='01a14429e3207acba34fc9c038cdcc90'
HEAD_BEVEL='01a14429e3207acba34fc9c038cdccd4'
TIP='067cd07f-7da1-48d7-a1ab-0855b9f9e6f2'
THREAD='01a14bba2edc78d48750ae1990993ef9'
FITTED='01a14bba2edc78d48750ae1990993f2e'
ROUND='01a14e7cd7da7524b64d13f9474be776'
FITTED_SKETCH='01a14bba2edc78d48750ae1990993f2d'
FITTED_DIAMETER='01a14e3bc1327622bfb7c077de93a831'
ROUND_SKETCH='01a14e7cd7da7524b64d13f9474be775'
ROUND_RADIUS='01a14e7cd7da7524b64d13f9474be7a1'
LANGUAGES=('cs','en','de','fr','ru')


def variants():
    source=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))['04-SCREWS/02-FITTED']['tables']
    dimensions,masses=source
    number=lambda value:float(value.replace(',','.'))
    coarse={10:1.5,12:1.75,14:2,16:2,18:2.5,20:2.5,22:2.5,24:3,27:3,30:3.5,33:3.5,36:4}
    result=[]
    for column,title in enumerate(dimensions[0][1:],1):
        designation=title.strip('()')
        ascii_size=designation.replace('×','x')
        diameter=int(ascii_size[1:].split('x')[0])
        pitch=float(ascii_size.split('x')[1]) if 'x' in ascii_size else coarse[diameter]
        lengths=[int(row[0]) for row in masses[1:] if row[0].isdigit() and row[column].strip() not in ('—','-','')]
        length=next(value for value in lengths if value>=50)
        for standard,row,short in [('021111',1,False),('021112',2,True)]:
            bands=[value.strip() for value in dimensions[row][column].split('|')]
            thread=number(bands[0 if length<=50 else 1])
            names=dict(zip(LANGUAGES,(
                'Lícovaný šroub s '+('krátkým' if short else 'dlouhým')+' závitem',
                'Fitted bolt with '+('short' if short else 'long')+' thread',
                'Passschraube mit '+('kurzem' if short else 'langem')+' Gewinde',
                'Boulon ajusté à filetage '+('court' if short else 'long'),
                'Призонный болт с '+('короткой' if short else 'длинной')+' резьбой')))
            result.append(dict(filename=f'HFB-CSN{standard}-{ascii_size}x{length:03d}.prtz',
                standard='ČSN 02 '+standard[2:],designation=designation,diameter_mm=diameter,
                pitch_mm=pitch,fine_pitch=pitch!=coarse[diameter],length_mm=length,
                fitted_diameter_mm=number(dimensions[4][column]),
                across_flats_mm=number(dimensions[6][column]),head_height_mm=number(dimensions[7][column]),
                underhead_radius_mm=number(dimensions[8][column]),thread_length_mm=thread,
                runout_mm=2*pitch,fit='n6',property_class='8.8',names=names,
                preferred=not title.startswith('('),table_column=column))
    for variant in result:
        variant.update(product_grade='A' if variant['diameter_mm']<=24 and variant['length_mm']<=min(10*variant['diameter_mm'],150) else 'B',
            bearing_face=False,bearing_diameter_mm=0,full_thread=False,thread_container=THREAD,
            tip_chamfer=dict(angle_degrees=30,distance_a_mm=.4*variant['pitch_mm']/1.5*math.sqrt(3)),
            tip_chamfer_parameters=dict(angle_degrees=60,distance_a_mm=.4*variant['pitch_mm']/1.5))
    return result


def gradual_dimension(native,sketch,dimension,start,end):
    steps=max(1,math.ceil(abs(math.log(end/start))/math.log(1.08)))
    for step in range(1,steps+1):
        native.run('sketch.dimension.set',sketch=sketch,dimension=dimension,
            value=start*(end/start)**(step/steps))


def author(native,variant,template,output):
    native.run('open',path=str(template))
    native.run('save_as',path=str(output));native.run('close');native.run('open',path=str(output))
    native.run('regenerate')
    # Replace the template's multi-curve face references through an undoable
    # native transaction. Every existing C/dimension retains its reference ID.
    for reference in native.run('sketch.entities',sketch=FITTED_SKETCH,limit=500)['items']:
        if reference['kind']!='external_reference':continue
        current=native.run('sketch.entity.get',sketch=FITTED_SKETCH,entity=reference['id'])['entity']
        key=current['source_semantic_key']
        kind='face'
        if key=='thread:surface:runout:end':key='thread:boundary:runout:end:end';kind='edge'
        elif not key.startswith('sketch-outline:'):key='sketch-outline:negative:'+key
        native.run('sketch.reference.replace',sketch=FITTED_SKETCH,reference=reference['id'],
            kind=kind,owner=current['source_owner_id'],key=key)
    for feature in (ROUND,FITTED,THREAD,TIP,HEAD_BEVEL):
        native.run('history.suppress',object=feature,suppressed=True)
    # Enlarge the shoulder before its source shaft; retain a positive chamfer
    # width throughout every independently solved authoring transaction.
    gradual_dimension(native,FITTED_SKETCH,FITTED_DIAMETER,11,variant['fitted_diameter_mm'])
    gradual_dimension(native,'01a14429e3207acba34fc9c038cdcc8f','01a14429e3207acba34fc9c038cdccb4',16,variant['across_flats_mm'])
    gradual_dimension(native,'01a14429e3207acba34fc9c038cdcc7e','01a14429e3207acba34fc9c038cdcc8c',10,variant['diameter_mm'])
    native_tools.extent(native,HEAD,1,variant['head_height_mm'])
    native_tools.extent(native,SHAFT,0,variant['length_mm'])
    native.run('history.suppress',object=HEAD_BEVEL,suppressed=False)
    native.run('chamfer.set',container=TIP,distance_a_mm=.4*variant['pitch_mm']/1.5)
    native.run('history.suppress',object=TIP,suppressed=False)
    native.run('shaft_thread.set',container=THREAD,designation=variant['designation'],
        end_condition='length',length_mm=variant['thread_length_mm'],runout_enabled=True,runout_pitch_factor=2)
    native.run('history.suppress',object=THREAD,suppressed=False)
    native.run('sketch.dimension.set',sketch=FITTED_SKETCH,dimension=FITTED_DIAMETER,text={'suffix':' n6'})
    native.run('history.suppress',object=FITTED,suppressed=False)
    gradual_dimension(native,ROUND_SKETCH,ROUND_RADIUS,1,variant['underhead_radius_mm'])
    native.run('history.suppress',object=ROUND,suppressed=False)
    parameters=native.run('document.parameters.get')['parameters']
    values={'name':variant['names'],'standard':{'':variant['standard']},'material':{'':variant['property_class']},'quantity':{'':'-'}}
    for parameter in parameters:
        if parameter['key'] in values:parameter['values']=values[parameter['key']]
    parameters=[parameter for parameter in parameters if parameter['key'] not in ('product_grade','fit')]
    parameters.insert(3,dict(key='fit',labels=dict(zip(LANGUAGES,('Tolerance dříku','Shank tolerance','Schafttoleranz','Tolérance de la tige','Допуск стержня'))),values={'':'n6'}))
    native.run('document.parameters.set',parameters=parameters)
    stock='"M" & d1 & "x" & d12'
    if variant['fine_pitch']:stock=f'"M" & d1 & "x{variant["pitch_mm"]:g}x" & d12'
    native.run('document.relations.set',relations=f'stock = {stock}\nmass = model.mass\n')
    native.run('regenerate')
    history=native.run('history.list')
    if history['calculation_errors']:raise RuntimeError(history['calculation_errors'])
    thread=native.run('shaft_thread.get',container=THREAD)
    assert thread['designation']==variant['designation'] and abs(thread['pitch_mm']-variant['pitch_mm'])<1e-9
    for sketch in native.run('sketch.list')['items']:
        for item in native.run('sketch.entities',sketch=sketch['sketch'],limit=500)['items']:
            if item['kind']=='external_reference':
                assert not native.run('sketch.entity.get',sketch=sketch['sketch'],entity=item['id'])['entity']['broken']
    native.run('save');native.run('close');native.run('open',path=str(output));native.run('regenerate')
    assert not native.run('history.list')['calculation_errors']
    parameters=native.run('document.parameters.get')
    native.run('save');native.run('close')
    return parameters


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--template',type=Path,required=True)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    parser.add_argument('--filename')
    args=parser.parse_args();stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True)
    source=args.template.resolve();source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
    template=stage/'evidence/template.prtz';template.parent.mkdir(parents=True,exist_ok=True)
    if not template.exists():shutil.copyfile(source,template)
    assert hashlib.sha256(template.read_bytes()).hexdigest()==source_hash
    rows=variants();(stage/'manifest.json').write_text(json.dumps({'variants':rows,'template_sha256':source_hash},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    for variant in rows:
        if args.filename and variant['filename']!=args.filename:continue
        output=stage/variant['filename']
        if output.exists():raise RuntimeError(f'Refusing to overwrite candidate: {output}')
        native=native_tools.Native(args.cli.resolve(),stage/'evidence'/output.stem)
        try:parameters=author(native,variant,template,output)
        finally:native.close()
        (stage/'evidence'/(output.name+'.parameters.json')).write_text(json.dumps(parameters,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(f'Authored {output.name}',flush=True)
    assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash


if __name__=='__main__':main()
