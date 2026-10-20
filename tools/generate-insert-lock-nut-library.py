"""Stage single steel Parts with a colored symbolic locking insert.

The insert is an ordinary second Body in the same native Part. The document's
steel density applies to both Bodies, as explicitly requested by the user.
Acceptance and factory installation are separate.
"""
import argparse
import configparser
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('hexnuts',ROOT/'tools/generate-hex-nut-library.py')
hexnuts=importlib.util.module_from_spec(spec);spec.loader.exec_module(hexnuts)
caps=hexnuts.caps


def variants():
    facts=json.loads((ROOT/'tools/data/insert-lock-nut-reviewed-dimensions.json').read_text(encoding='utf-8'))
    result=[]
    for row in facts['rows']:
        v=dict(zip(facts['columns'],row));d=v['diameter_mm'];p=v['pitch_mm'];s=v['across_flats_mm']
        v.update(filename=f'NILN-ISO10511-M{d:04.1f}.prtz',standard=facts['standard'],family='07-LOCKING',
            steel_density_kg_mm3=facts['steel_density_kg_mm3'],
            mass_representation='Homogeneous steel Part, including the colored symbolic insert',
            names=dict(zip(caps.LANGUAGES,('Nízká samojistná matice s nekovovou vložkou',
                'Thin lock nut with non-metallic insert','Niedrige selbstsichernde Mutter mit nichtmetallischem Einsatz',
                'Écrou bas autofreiné à insert non métallique','Низкая самоконтрящаяся гайка с неметаллической вставкой'))))
        v.update(collar_outside_radius_mm=.46*s,collar_wall_mm=.3*p,lip_mm=.2*p,corner_radius_mm=.125*p,
            lip_bore_radius_mm=d/2+.15*p,insert_bore_radius_mm=d/2-.1*p,chamfer_degrees=30)
        result.append(v)
    return result


def parameters(native,v,names,material):
    rows=native.run('document.parameters.get')['parameters']
    values=dict(name=names,standard={'':v['standard']},stock={'':f'M{v["diameter_mm"]:g}x{v["pitch_mm"]:g}'},material={'':material},quantity={'':'-'})
    for row in rows:
        if row['key'] in values:row['values']=values[row['key']]
    native.run('document.parameters.set',parameters=rows)
    native.run('document.settings.set',precision=dict(decimal_places=6))


def density(native,name,value):
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=name),
        dict(key='MASS_DENSITY',value=str(value),unit='kg/mm^3',descriptions=dict(zip(caps.LANGUAGES,
            ('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])


def independent_shell_volume(v):
    s,h,z=v['across_flats_mm'],v['height_mm'],v['hex_height_mm'];r=v['bore_mm']/2
    t=math.tan(math.pi/6);sec=1/math.cos(math.pi/6);a=s/2
    removed=12*t*a**3*((sec*t+math.log(sec+t))/3-t+math.pi/18)
    mouth=v['mouth_depth_mm']
    bore=math.pi*r*r*z+math.pi*(r*math.tan(math.pi/3)*mouth**2+math.tan(math.pi/3)**2*mouth**3/3)
    ro=v['collar_outside_radius_mm'];rc=ro-v['collar_wall_mm'];ri=v['lip_bore_radius_mm'];lip=v['lip_mm'];R=v['corner_radius_mm']
    collar=math.pi*((ro*ro-rc*rc)*(h-lip-z)+(ro*ro-ri*ri)*lip)
    collar-=math.pi*((2-math.pi/2)*ro*R*R+(-5/3+math.pi/2)*R**3)
    return math.sqrt(3)*s*s*z/2-removed-bore+collar


def check_volume(native,expected):
    actual=native.run('body_properties.create')['volume_mm3'];native.run('undo')
    assert abs(actual-expected)<max(1e-5,expected*1e-8),(actual,expected)


def finish(native,target):
    native.run('document.relations.set',relations='mass = model.mass\n')
    native.run('regenerate');assert not native.run('history.list').get('calculation_errors',{})
    native.run('save');native.run('close')
    rows=native.run('open',path=str(target));identifier=next(d['id'] for d in rows if d['active'])
    native.run('regenerate');native.run('save')
    mass=float(next(p['values'][''] for p in native.run('document.parameters.get')['parameters'] if p['key']=='mass'))
    assert mass>0
    return identifier,mass


def crown(native,v):
    ro=v['collar_outside_radius_mm'];rc=ro-v['collar_wall_mm'];ri=v['lip_bore_radius_mm']
    z=v['hex_height_mm'];h=v['height_mm'];lip=v['lip_mm']
    assert 0<ri<rc<ro<v['across_flats_mm']/2 and z<h-lip
    sk=native.run('sketch.create',name='Steel retaining collar meridian',plane='XZ')['sketch']
    p=[[rc,-z],[ro,-z],[ro,-h],[ri,-h],[ri,-h+lip],[rc,-h+lip]]
    lines=[native.run('sketch.segment.create',sketch=sk,first=a,second=p[(i+1)%len(p)],snap_mm=1e-8)['geometry'] for i,a in enumerate(p)]
    entity=lambda key:native.run('sketch.entity.get',sketch=sk,entity=key)['entity']
    points=[entity(g)['first'] for g in lines]
    for i,g in enumerate(lines):native.run('sketch.constraint.create',sketch=sk,kind='horizontal' if i%2==0 else 'vertical',geometry=[g])
    dims={}
    for key,index,kind,axis,value in [('cavity',0,'distance_x','y',rc),('outside',1,'distance_x','y',ro),
        ('lip_bore',3,'distance_x','y',ri),('floor',0,'distance_y','x',-z),('top',2,'distance_y','x',-h),
        ('lip_floor',4,'distance_y','x',-h+lip)]:
        dims[key]=native.run('sketch.dimension.create',sketch=sk,kind=kind,points=[points[index]],geometry=['sketch_axis:'+axis],value=value)['dimension']
    rounded=native.run('sketch.corner_fillet.create',sketch=sk,first=lines[1],second=lines[2],radius_mm=v['corner_radius_mm'])['geometry']
    dims['round']=native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[rounded],value=v['corner_radius_mm'])['dimension']
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,0],second=[0,1],construction=True,snap_mm=1e-8)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(axis)['first'],'sketch_origin'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis])
    native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[axis],value=1)
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    side=caps.side(operation='revolution');side.update(rotation_extent='full',angle_degrees=360)
    feature=native.run('feature.create',sketch=sk,parameters=dict(axis_segment_id=axis,symmetric=False,sides=[side,caps.side(operation='none')]))['container']
    return dict(sketch=sk,feature=feature,dimensions=dims)


def author_shell(native,v,target):
    thread=next(t for t in native.run('thread.catalog',standard='metric',limit=1000)['items']
        if abs(t['nominal_diameter_mm']-v['diameter_mm'])<1e-8 and abs(t['pitch_mm']-v['pitch_mm'])<1e-8)
    v['bore_mm']=thread['internal_root_diameter_mm']
    mouth=(v['diameter_mm']-v['bore_mm'])/2/math.tan(math.pi/3)
    v['mouth_depth_mm']=mouth
    bevel_height=v['across_flats_mm']*(1/math.sqrt(3)-.5)*math.tan(math.pi/6)
    v['hex_height_mm']=max(v['minimum_thread_height_mm']+mouth+.05*v['pitch_mm'],v['minimum_wrench_height_mm']+2*bevel_height+.01*v['pitch_mm'])
    assert v['hex_height_mm']-mouth>=v['minimum_thread_height_mm']
    assert v['hex_height_mm']-2*bevel_height>=v['minimum_wrench_height_mm']
    native.run('cd',path=str(target.parent));native.run('new',type='part',name=target.stem)
    sk=native.run('sketch.create',name='Steel hexagon',plane='XY')['sketch']
    native.run('sketch.polygon.create',sketch=sk,center=[0,0],rim=[v['across_flats_mm']/math.sqrt(3),0],sides=6)
    items=native.run('sketch.entities',sketch=sk)['items'];entity=lambda g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
    circle=next(e for e in items if e['kind']=='circle')
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[entity(circle['id'])['center'],'sketch_origin'])
    size=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle['id']],value=2*v['across_flats_mm']/math.sqrt(3))['dimension']
    vertex=next(e['id'] for e in items if e['kind']=='point' and abs(entity(e['id'])['y'])<1e-8 and entity(e['id'])['x']>0)
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[vertex],geometry=['sketch_axis:x'])
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    prism=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,sides=[caps.side(v['hex_height_mm']),caps.side(operation='none')]))['container']
    b=dict(v,height_mm=v['hex_height_mm']);bevels=[hexnuts.bevel(native,b,prism,top) for top in (False,True)]
    collar=crown(native,v)
    opening=native.run('opening.create',type='metric',designation=thread['designation'],bore_end='through_all',thread_end='length',
        bore_length_mm=v['hex_height_mm'],thread_length_mm=v['hex_height_mm'],runout_pitch_factor=0,
        chamfer_enabled=True,chamfer_depth_mm=mouth,chamfer_angle_degrees=120,drill_point_enabled=False,placement=dict(z=0))['container']
    v['shell_volume_mm3']=independent_shell_volume(v);check_volume(native,v['shell_volume_mm3'])
    return dict(body=native.run('body.list')['active_body'],hexagon=sk,size=size,prism=prism,bevels=bevels,collar=collar,thread=opening)


def author_insert(native,v):
    body=native.run('body.create',name='Symbolic locking insert')['body']
    sk=native.run('sketch.create',name='Symbolic insert annulus',plane='XY')['sketch']
    dims={};circles={}
    for key,r in [('outside',v['collar_outside_radius_mm']-v['collar_wall_mm']),('inside',v['insert_bore_radius_mm'])]:
        g=native.run('sketch.circle.create',sketch=sk,center=[0,0],radius_mm=r,snap_mm=1e-8)['geometry'];circles[key]=g
        center=native.run('sketch.entity.get',sketch=sk,entity=g)['entity']['center']
        if key=='outside':native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[center,'sketch_origin'])
        dims[key]=native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[g],value=2*r)['dimension']
    assert native.run('sketch.solve_status',sketch=sk)['remaining_degrees_of_freedom']==0
    height=v['height_mm']-v['lip_mm']-v['hex_height_mm'];assert height>0
    feature=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,profile_plane_offset=v['hex_height_mm'],sides=[caps.side(height),caps.side(operation='none')]))['container']
    v['insert_volume_mm3']=math.pi*((v['collar_outside_radius_mm']-v['collar_wall_mm'])**2-v['insert_bore_radius_mm']**2)*height
    check_volume(native,v['insert_volume_mm3'])
    native.run('appearance.set',body=body,style=dict(color='#2455a4',roughness=.6,metallic=0))
    return dict(body=body,sketch=sk,dimensions=dims,feature=feature)


def relations(native,target,ids):
    saved=configparser.RawConfigParser();saved.read(target,encoding='utf-8')
    entries=json.loads(saved['Document']['dimension_identifiers'])['entries']
    def symbol(owner,key):
        return 'd'+str(next(e['number'] for e in entries if e['owner']==owner and e['key']==key))
    shell,insert=ids['shell'],ids['insert'];collar=shell['collar']
    size=symbol(shell['hexagon'],'dimension:'+shell['size'])
    height=symbol(shell['prism'],'parameter:side0_length')
    diameter=symbol(shell['thread'],'parameter:thread_designation')
    pitch=symbol(shell['thread'],'parameter:pitch')
    dim=lambda k:symbol(collar['sketch'],'dimension:'+collar['dimensions'][k])
    lines=['mass = model.mass',f'stock = "M" & {diameter} & "x" & {pitch}',
        f'{dim("outside")} = {size} * sqrt(3) / 2 * 0.46',
        f'{dim("cavity")} = {dim("outside")} - 0.3 * {pitch}',
        f'{dim("lip_bore")} = {diameter} / 2 + 0.15 * {pitch}',
        f'{dim("floor")} = -{height}',f'{dim("lip_floor")} = {dim("top")} + 0.2 * {pitch}',
        f'{dim("round")} = 0.125 * {pitch}']
    for key in ('bore_length','thread_length'):
        lines.append(f'{symbol(shell["thread"],"parameter:"+key)} = {height}')
    for b in shell['bevels']:
        lines.append(f'{symbol(b["sketch"],"dimension:"+b["width"])} = {size} * (1 - sqrt(3) / 2) / 2')
    lines.extend([
        f'{symbol(insert["sketch"],"dimension:"+insert["dimensions"]["outside"])} = 2 * {dim("cavity")}',
        f'{symbol(insert["sketch"],"dimension:"+insert["dimensions"]["inside"])} = {diameter} - 0.2 * {pitch}',
        f'{symbol(insert["feature"],"parameter:profile_offset")} = {height}',
        f'{symbol(insert["feature"],"parameter:side0_length")} = -{dim("lip_floor")} - {height}'])
    native.run('document.relations.set',relations='\n'.join(lines)+'\n')
    native.run('regenerate');assert not native.run('history.list').get('calculation_errors',{})
    native.run('save')


def author(native,v,stage):
    target=stage/v['filename']
    shell=author_shell(native,v,target);insert=author_insert(native,v)
    density(native,'Steel',v['steel_density_kg_mm3']);parameters(native,v,v['names'],'Steel')
    # Leave the main metal Body active for ordinary later editing.
    native.run('body.activate',body=shell['body'])
    identifier,mass=finish(native,target);ids=dict(shell=shell,insert=insert,document=identifier)
    relations(native,target,ids)
    expected=(v['shell_volume_mm3']+v['insert_volume_mm3'])*v['steel_density_kg_mm3']
    assert abs(mass-expected)<=.50001e-6,(mass,expected)
    native.run('close');native.run('open',path=str(target))
    assert native.run('body.list')['active_body']==shell['body']
    native.run('body.activate',body=insert['body'])
    assert native.run('appearance.get')['style']['color'].lower()=='#2455a4'
    native.run('body.activate',body=shell['body']);native.run('save');native.run('close')
    v.update(saved_mass_kg=mass,expected_mass_kg=expected)
    return ids


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--diameter',type=float)
    a=p.parse_args();stage=a.stage.resolve();assert stage.is_relative_to(ROOT/'build') and not stage.exists()
    stage.mkdir(parents=True);rows=[v for v in variants() if a.diameter is None or v['diameter_mm']==a.diameter];completed=[];assert rows
    for v in rows:
        evidence=stage/'evidence'/Path(v['filename']).stem
        native=caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',evidence,settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=author(native,v,stage)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
        completed.append(v);(stage/'manifest.json').write_text(json.dumps(dict(variants=completed),ensure_ascii=False,indent=2),encoding='utf-8')
        print('Authored single steel Part lock nut '+v['filename'],flush=True)


if __name__=='__main__':main()
