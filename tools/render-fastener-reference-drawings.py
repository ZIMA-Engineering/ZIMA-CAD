"""Create dimensioned native reference Drawings from verified fastener Parts."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fastener_author',ROOT/'tools/generate-fastener-library.py')
author=importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)


def attachment(curve,kind='center',parameter=0):
    return dict(kind=kind,reference={k:curve[k] for k in ('owner','key','instance_path')},parameter=parameter)


def create(native,original,part,variant,target):
    native.run('open',path=str(original))
    native.run('save_as',path=str(part))
    native.run('close')
    source=next(v['id'] for v in native.run('open',path=str(part)) if v['active'])
    native.run('new',type='drawing',name=target.stem)
    sheet=native.run('drawing.sheet.list')['items'][0]['sheet']
    paper=native.run('drawing.sheet.set',sheet=sheet,format='A3',locale='en',thick_line_mm=.5,thin_line_mm=.25)
    width,height=paper['width_mm'],paper['height_mm']
    native.run('drawing.frame.clear',sheet=sheet)
    native.run('drawing.title_block.clear',sheet=sheet)
    d,l,k,s=[variant[key] for key in ('diameter_mm','length_mm','head_height_mm','across_flats_mm')]
    scale=min(3,100/(l+k))
    side=native.run('drawing.view.create',sheet=sheet,source=source,
        camera=dict(horizontal=[0,0,1],vertical=[1,0,0],depth=[0,-1,0]),
        x_mm=width-40,y_mm=height-75,scale=scale,display_style='hidden_edges')['view']
    end=native.run('drawing.view.create',sheet=sheet,source=source,
        camera=dict(horizontal=[1,0,0],vertical=[0,1,0],depth=[0,0,-1]),
        x_mm=width-185,y_mm=height-75,scale=scale,display_style='visible_edges')['view']
    horizontal=[1/math.sqrt(2),-1/math.sqrt(2),0]
    vertical=[1/math.sqrt(6),1/math.sqrt(6),-math.sqrt(2/3)]
    iso_camera=dict(horizontal=[(h-v)/math.sqrt(2) for h,v in zip(horizontal,vertical)],
        vertical=[(h+v)/math.sqrt(2) for h,v in zip(horizontal,vertical)],
        depth=[-1/math.sqrt(3),-1/math.sqrt(3),-1/math.sqrt(3)])
    iso_center_x=iso_camera['horizontal'][2]*(l-k)/2
    iso_center_y=iso_camera['vertical'][2]*(l-k)/2
    native.run('drawing.view.create',sheet=sheet,source=source,camera=iso_camera,
        x_mm=width-275+iso_center_x*scale,y_mm=height-85-iso_center_y*scale,
        scale=scale,display_style='visible_edges')
    refs=native.run('drawing.view.references',view=side,kind='curve')['items']
    circles=[r for r in refs if r['circle'] and abs(r['circle']['normal'][2])>.999]
    def plane(z,radius=None):
        found=[r for r in circles if abs(r['circle']['center'][2]-z)<1e-6 and
            (radius is None or abs(r['circle']['radius_mm']-radius)<1e-6)]
        if not found:raise RuntimeError(f'No real circular reference at z={z}, r={radius}')
        return min(found,key=lambda r:r['circle']['radius_mm'])
    def dimension(symbol,view,attachments,value,kind='linear',direction='horizontal',position=None):
        args=dict(view=view,attachments=attachments,kind=kind,style=dict(text_override=symbol,suffix=''))
        if kind=='linear':args['direction']=direction
        if position is not None:args['placements']=[position]
        result=native.run('drawing.dimension.create',**args)
        if result['state']!='resolved' or abs(result['measurements'][0]['value']-value)>1e-6:
            raise RuntimeError(f'Unresolved or incorrect {symbol} dimension: {result}')
    bearing=plane(0,d/2+variant['underhead_radius_mm'])
    tip=plane(l)
    head=plane(-k)
    dimension('l',side,[attachment(bearing),attachment(tip)],l,position=[l/2,s*.8])
    dimension('k',side,[attachment(head),attachment(bearing)],k,position=[-k/2,-s*.8])
    shaft=plane(l-variant['tip_chamfer']['distance_a_mm'],d/2)
    dimension('d',side,[attachment(shaft,'curve_point',0),attachment(shaft,'curve_point',.5)],d,
        direction='vertical',position=[l+5/scale,0])
    end_refs=native.run('drawing.view.references',view=end,kind='curve')['items']
    flats=[]
    raised=variant.get('bearing_face',variant['product_grade']!='C')
    flat_z=-variant['bearing_height_mm'] if raised else 0
    for y in (-s/2,s/2):
        candidates=[r for r in end_refs if r['line'] and
            abs(r['first'][1]-y)<1e-6 and abs(r['last'][1]-y)<1e-6 and
            abs(r['first'][2]-flat_z)<1e-6 and abs(r['last'][2]-flat_z)<1e-6 and
            abs(r['first'][0]-r['last'][0])>s*.1]
        if not candidates:raise RuntimeError('No real hexagon flat for s')
        flats.append(min(candidates,key=lambda r:abs(r['first'][2])))
    dimension('s',end,[attachment(r,'curve_point',.5) for r in flats],s,
        direction='vertical',position=[s*.9,0])
    native.run('save')
    native.run('export.image',path=str(target.with_suffix('.png')),sheet=sheet,dpi=150,
        crop_mm=[20,30,300,110])
    native.run('close',discard=True)
    native.run('open',path=str(target))
    dimensions=native.run('drawing.dimension.list',sheet=sheet)['items']
    if len(dimensions)!=4 or any(v['state']!='resolved' for v in dimensions):
        raise RuntimeError('Reference Drawing dimensions failed native reopening')
    native.run('close')
    native.run('close')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli',type=Path,required=True)
    parser.add_argument('--checker',type=Path,required=True)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--stage',type=Path,action='append',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--standard')
    args=parser.parse_args()
    manifest=json.loads(args.manifest.read_text(encoding='utf-8'))
    standards=sorted({v['standard'] for v in manifest['variants']})
    for standard in standards:
        if args.standard and standard!=args.standard:continue
        variant=next(v for v in manifest['variants'] if v['standard']==standard and
            v['diameter_mm']==8 and not v['fine_pitch']) if standard.startswith('ČSN') else next(
            v for v in manifest['variants'] if v['standard']==standard and v['diameter_mm']==8)
        matches=[p.resolve()/variant['filename'] for p in args.stage if (p/variant['filename']).is_file()]
        if len(matches)!=1:raise RuntimeError(f'Expected one source for {standard}')
        source=matches[0];digest=hashlib.sha256(source.read_bytes()).hexdigest()
        folder=args.output.resolve()/standard.replace('ČSN','CSN').replace(' ','')
        folder.mkdir(parents=True,exist_ok=True)
        part=folder/(Path(variant['filename']).stem+'-REFERENCE.prtz')
        if part.exists():raise RuntimeError('Use a fresh output directory')
        target=part.with_suffix('.drwz')
        native=author.Native(args.cli.resolve(),folder/'evidence')
        try:create(native,source,part,variant,target)
        finally:
            native.close()
            if hashlib.sha256(source.read_bytes()).hexdigest()!=digest:raise RuntimeError('Source changed')
        subprocess.run([str(args.checker.resolve()),'--rebase-reference-drawing',str(target)],check=True)
        print(f'Created and reopened {standard} native reference Drawing',flush=True)


if __name__=='__main__':main()
