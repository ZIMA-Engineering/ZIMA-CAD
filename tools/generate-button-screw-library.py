"""Author reviewed A2 button-head screws with an exact revolved crown."""
import argparse
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('caps',ROOT/'tools/generate-cap-screw-library.py')
caps=importlib.util.module_from_spec(spec);spec.loader.exec_module(caps)


def variants():
    d=json.loads((ROOT/'tools/data/button-screw-reviewed-dimensions.json').read_text(encoding='utf-8'));rows=[]
    for i,diameter in enumerate(d['diameters_mm']):
        pitch=d['pitch_mm'][i];length=d['default_lengths_mm'][i]
        radius=(d['crown_radius_min_mm'][i]+d['crown_radius_max_mm'][i])/2
        k=d['head_height_max_mm'][i];top=d['top_diameter_ref_mm'][i]/2
        center=-k+math.sqrt(radius*radius-top*top)
        rows.append(dict(filename=f'BHCS-ISO7380-1-M{diameter:02d}x{length:03d}.prtz',family='04-BUTTON-A2',
            standard='ISO 7380-1',designation=f'M{diameter}',diameter_mm=diameter,pitch_mm=pitch,
            head_diameter_mm=d['head_diameter_max_mm'][i],head_height_mm=k,
            underhead_radius_mm=d['underhead_radius_full_min_mm'][i],head_edge_radius_mm=radius,
            head_profile='spherical_crown',crown_center_z_mm=center,top_diameter_mm=2*top,
            drive_width_mm=d['socket_width_min_mm'][i],drive_depth_mm=d['socket_depth_min_mm'][i],
            head_floor_min_mm=d['head_floor_min_mm'][i],
            socket_depth_max_mm=d['socket_depth_max_mm'][i],
            socket_entry_kind='geometry_corner',socket_entry_radius_mm=d['socket_width_min_mm'][i]/math.sqrt(3),
            socket=True,length_mm=length,thread_length_mm=length-d['underhead_radius_full_min_mm'][i]-2*pitch,full_thread=True,
            material_file='config/materials/01_steels/stainless/1.4301_AISI304.matz',density_kg_mm3=7.9e-6,
            property_class='A2',preferred=True,names=dict(zip(caps.LANGUAGES,(
                'Šroub s půlkulatou hlavou a vnitřním šestihranem',
                'Hexagon socket button head screw',
                'Schraube mit Flachrundkopf und Innensechskant',
                'Vis à tête bombée à six pans creux',
                'Винт с полукруглой головкой и внутренним шестигранником')))))
    return rows


def head(native,v):
    sk=native.run('sketch.create',name='Button head profile',plane='XZ')['sketch']
    r=v['head_diameter_mm']/2;top=v['top_diameter_mm']/2;k=v['head_height_mm'];radius=v['head_edge_radius_mm'];center=v['crown_center_z_mm']
    z=center-math.sqrt(radius*radius-r*r);assert -k<z<0
    segments=[]
    xy=lambda p:[p[0],-p[1]]  # XZ Sketch local Y points towards global -Z.
    for first,second in [([0,0],[r,0]),([r,0],[r,z]),([top,-k],[0,-k]),([0,-k],[0,0])]:
        segments.append(native.run('sketch.segment.create',sketch=sk,first=xy(first),second=xy(second))['geometry'])
    arc=native.run('sketch.arc.create',sketch=sk,center=xy([0,center]),start=xy([r,z]),end=xy([top,-k]),clockwise=False)['geometry']
    axis=native.run('sketch.segment.create',sketch=sk,first=[0,0],second=[0,-2*k],construction=True)['geometry']
    native.run('sketch.segment.centerline',sketch=sk,segment=axis,centerline=True)
    entity=lambda item:native.run('sketch.entity.get',sketch=sk,entity=item)['entity']
    origin=entity(segments[0])['first'];arc_center=entity(arc)['center']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[origin,'sketch_origin'])
    for kind,items in [('horizontal',(segments[0],segments[2])),('vertical',(segments[1],segments[3],axis))]:
        for item in items:native.run('sketch.constraint.create',sketch=sk,kind=kind,geometry=[item])
    for item,value in [(segments[0],r),(segments[2],top),(segments[3],k),(axis,2*k)]:
        native.run('sketch.dimension.create',sketch=sk,kind='distance',geometry=[item],value=value)
    native.run('sketch.constraint.create',sketch=sk,kind='point_on_line',points=[arc_center],geometry=['sketch_axis:y'])
    native.run('sketch.dimension.create',sketch=sk,kind='radius',geometry=[arc],value=radius)
    forward=caps.side(operation='revolution');forward.update(rotation_extent='full',angle_degrees=360)
    return native.run('feature.create',sketch=sk,parameters=dict(axis_segment_id=axis,symmetric=False,
        sides=[forward,caps.side(operation='none')]))['container']


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe');p.add_argument('--filename');args=p.parse_args()
    stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);rows=variants()
    (stage/'manifest.json').write_text(json.dumps(dict(variants=rows),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    for v in rows:
        if args.filename and args.filename!=v['filename']:continue
        target=stage/v['filename']
        if target.exists():
            assert (stage/'evidence'/target.stem/'identities.json').is_file(),target
            continue
        native=caps.studs.api.Native(args.cli.resolve(),stage/'evidence'/target.stem,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:identity=caps.author(native,v,target,head_builder=head)
        finally:native.close()
        (stage/'evidence'/target.stem/'identities.json').write_text(json.dumps(identity,indent=2),encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)


if __name__=='__main__':main()
