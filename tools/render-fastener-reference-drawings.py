"""Derive localized factory illustrations from an authored native Drawing pattern."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fastener_author',ROOT/'tools/generate-fastener-library.py')
author=importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)
LANGUAGES=('cs','en','de','fr','ru')
DEFAULT_PATTERN=author.LIBRARY/'03-FULL-AB/0000-index/HHS-ISO4017-M08x035-REFERENCE.drwz'


def attachment(curve,kind='center',parameter=0):
    return dict(kind=kind,reference={k:curve[k] for k in ('owner','key','instance_path')},parameter=parameter)


def measure_map(native,sheet):
    result={}
    for dimension in native.run('drawing.dimension.list',sheet=sheet)['items']:
        text=dimension['measurements'][0]['text'] if dimension['measurements'] else '?'
        result[text]=dimension
    return result


def create(native,original,part,variant,target,pattern):
    native.run('open',path=str(original));native.run('save_as',path=str(part));native.run('close')
    source=next(v['id'] for v in native.run('open',path=str(part)) if v['active'])
    # Drawing Save As deliberately copies its own source Part too. Keep that
    # temporary pair separate; adopt the verified representative only after
    # every real view and dimension has been retargeted through native commands.
    temporary=target.with_name('TEMPLATE-COPY-'+target.name)
    native.run('open',path=str(pattern));native.run('save_as',path=str(temporary));native.run('close')
    native.run('open',path=str(temporary))
    sheets=native.run('drawing.sheet.list')['items'];assert len(sheets)==1
    sheet=sheets[0];sid=sheet['sheet'];width,height=sheet['width_mm'],sheet['height_mm']
    views=native.run('drawing.view.list')['items'];assert len(views)==2
    parent=next(v for v in views if not v.get('parent_view'))
    child=next(v for v in views if v.get('parent_view'))
    assert child['parent_view']==parent['view']
    d,l,k,s=[variant[key] for key in ('diameter_mm','length_mm','head_height_mm','across_flats_mm')]
    raised=variant.get('bearing_face',variant['product_grade']!='C')
    historical=variant['standard'].startswith('ČSN')
    for symbol,dimension in measure_map(native,sid).items():
        if not raised and symbol in ('c','dw'):
            native.run('drawing.dimension.delete',dimension=dimension['dimension'])
        elif historical and symbol in ('r','u'):
            native.run('drawing.dimension.set',dimension=dimension['dimension'],
                style=dict(text_override='R' if symbol=='r' else 'z1'))
    native.run('drawing.frame.clear',sheet=sid);native.run('drawing.title_block.clear',sheet=sid)
    scale=min(110/(l+k),35/(2*s/math.sqrt(3)))
    native.run('drawing.view.set',view=parent['view'],source=source,scale=scale,
        x_mm=width-((20 if raised else 25)+k*scale),y_mm=height-60)
    # Raised bearing diameters need extra space before the projected head view.
    native.run('drawing.view.set',view=child['view'],distance_mm=l*scale+(25 if raised else 15)+s*scale/2,scale=scale)
    refs=native.run('drawing.view.references',view=parent['view'],kind='curve')['items']
    circles=[r for r in refs if r.get('circle')]
    def plane(z,radius=None):
        found=[r for r in circles if abs(r['circle']['normal'][2])>.999 and
            abs(r['circle']['center'][2]-z)<1e-6 and
            (radius is None or abs(r['circle']['radius_mm']-radius)<1e-6)]
        if not found:raise RuntimeError(f'No actual reference circle at z={z}, r={radius}')
        return min(found,key=lambda r:r['circle']['radius_mm'])
    dims=measure_map(native,sid)
    end_refs=native.run('drawing.view.references',view=child['view'],kind='curve')['items']
    flat_z=-variant['bearing_height_mm'] if raised else 0
    flats=[]
    for y in (-s/2,s/2):
        found=[r for r in end_refs if r.get('line') and
            abs(r['first'][1]-y)<1e-5 and abs(r['last'][1]-y)<1e-5 and
            abs(r['first'][2]-flat_z)<1e-5 and abs(r['last'][2]-flat_z)<1e-5 and
            abs(r['first'][0]-r['last'][0])>s*.1]
        if not found:raise RuntimeError('Current physical hexagon flat is missing')
        flats.append(min(found,key=lambda r:r['key']))
    native.run('drawing.dimension.set',dimension=dims['s']['dimension'],direction='horizontal',
        attachments=[attachment(r,'curve_point',.5) for r in flats],
        placements=[[0,-s/math.sqrt(3)-7/scale]])
    corners=[]
    for x in (-s/math.sqrt(3),s/math.sqrt(3)):
        found=[(r,parameter) for r in end_refs if r.get('line')
            for parameter,point in ((0,r['first']),(1,r['last']))
            if abs(point[0]-x)<1e-5 and abs(point[1])<1e-5 and abs(point[2]-flat_z)<1e-5]
        if not found:raise RuntimeError('Current physical hexagon corner is missing')
        curve,parameter=min(found,key=lambda item:(item[0]['key'],item[1]))
        corners.append(attachment(curve,'curve_point',parameter))
    native.run('drawing.dimension.set',dimension=dims['(e)']['dimension'],direction='vertical',
        attachments=corners,placements=[[s/2+7/scale,0]])
    radius_symbol='R' if historical else 'r'
    if dims[radius_symbol]['state']!='resolved':
        arcs=[r for r in circles if abs(r['circle']['radius_mm']-variant['underhead_radius_mm'])<1e-6 and
            abs(r['circle']['normal'][1])>.999]
        if not arcs:raise RuntimeError('No current underhead-radius contour')
        arc=max(arcs,key=lambda r:r['circle']['center'][0])
        native.run('drawing.dimension.set',dimension=dims[radius_symbol]['dimension'],attachments=[attachment(arc,'tangent')])
    # Keep the radius leader above the neighbouring dimension extension lines.
    # Paper-space clearance stays consistent when representative sizes change.
    native.run('drawing.dimension.set',dimension=dims[radius_symbol]['dimension'],
        placements=[[8/scale,s/2+17/scale]])
    if historical:
        for dimension in dims.values():
            if dimension['kind']=='angular':
                native.run('drawing.dimension.set',dimension=dimension['dimension'],
                    layouts=[dict(arrows_reversed=True)])
    if not variant['full_thread']:
        start=plane(l-variant['thread_length_mm']);end=plane(l)
        native.run('drawing.dimension.create',view=parent['view'],kind='linear',direction='horizontal',
            attachments=[attachment(start),attachment(end)],style=dict(text_override='b'),
            placements=[[l-variant['thread_length_mm']/2,s/2+17/scale]])
    if 'locking_hole_diameter_mm' in variant:
        holes=[r for r in circles if abs(r['circle']['normal'][1])>.999 and
            abs(r['circle']['radius_mm']-variant['locking_hole_diameter_mm']/2)<1e-6 and
            abs(r['circle']['center'][2]+k/2)<1e-6]
        if not holes:raise RuntimeError('Actual centered head-hole circle missing')
        hole=max(holes,key=lambda r:r['circle']['center'][1])
        native.run('drawing.dimension.create',view=parent['view'],kind='diameter',
            attachments=[attachment(hole)],style=dict(text_override='d1'),
            placements=[[-k-5/scale,-s*.15]])
    dims=measure_map(native,sid)
    expected={'l':l,'k':k,'d':d,'s':s,'(e)':2*s/math.sqrt(3),radius_symbol:variant['underhead_radius_mm'],
        'z1' if historical else 'u':variant['tip_chamfer']['distance_a_mm']}
    if raised:expected.update(c=variant['bearing_height_mm'],dw=variant['bearing_diameter_mm'])
    if not variant['full_thread']:expected['b']=variant['thread_length_mm']
    if 'locking_hole_diameter_mm' in variant:expected['d1']=variant['locking_hole_diameter_mm']
    for symbol,value in expected.items():
        row=dims.get(symbol)
        if not row or row['state']!='resolved' or abs(row['measurements'][0]['value']-value)>1e-5*max(1,abs(value)):
            raise RuntimeError(f'Incorrect native {symbol}: {row}, expected {value}')
    angles=[v for v in dims.values() if v['kind']=='angular']
    assert len(angles)==1 and angles[0]['state']=='resolved'
    assert abs(angles[0]['measurements'][0]['value']-variant.get('head_chamfer_degrees',30))<1e-5
    native.run('save');native.run('close');native.run('close')
    return temporary,dict(sheet=sid,source=source,expected=expected,dimensions=dims,scale=scale,width=width,height=height)


def illustration_texts(variant,details,language,labels):
    width,height=details['width'],details['height']
    title=variant['standard']+' · '+variant['designation']+' · '+variant['names'][language]
    # Long translated standard names must retain a white right margin too.
    texts=[dict(text=title,x_mm=width-20,y_mm=height-97,height_mm=2.0 if len(title)>100 else 2.5)]
    ordered=[s for s in ('l','k','d','s','(e)','r','R','c','dw','u','z1','b','d1') if s in details['expected']]
    for index,symbol in enumerate(ordered):
        key='e' if symbol=='(e)' else 'r' if symbol=='R' else symbol
        text=symbol+' — '+labels[key][language]
        texts.append(dict(text=text,x_mm=width-(20 if index%2==0 else 108),
            y_mm=height-(104+4.4*(index//2)),height_mm=2.2))
    return dict(locale=language,texts=texts)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli',type=Path,required=True);parser.add_argument('--checker',type=Path,required=True)
    parser.add_argument('--manifest',type=Path,required=True);parser.add_argument('--stage',type=Path,action='append',required=True)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--standard')
    parser.add_argument('--pattern',type=Path,default=DEFAULT_PATTERN)
    args=parser.parse_args();manifest=json.loads(args.manifest.read_text(encoding='utf-8'))
    labels=json.loads((ROOT/'tools/data/hex-fastener-illustrations.json').read_text(encoding='utf-8'))['legend']
    pattern=args.pattern.resolve();pattern_hash=hashlib.sha256(pattern.read_bytes()).hexdigest()
    accepted=[]
    for standard in sorted({v['standard'] for v in manifest['variants']}):
        if args.standard and standard!=args.standard:continue
        variant=next(v for v in manifest['variants'] if v['standard']==standard and v['diameter_mm']==8)
        matches=[p.resolve()/variant['filename'] for p in args.stage if (p/variant['filename']).is_file()]
        if len(matches)!=1:raise RuntimeError('Expected one verified source for '+standard)
        source=matches[0];digest=hashlib.sha256(source.read_bytes()).hexdigest()
        folder=args.output.resolve()/standard.replace('ČSN','CSN').replace(' ','');folder.mkdir(parents=True,exist_ok=True)
        part=folder/(Path(variant['filename']).stem+'-REFERENCE.prtz');target=part.with_suffix('.drwz')
        if part.exists() or target.exists():raise RuntimeError('Use a fresh illustration output directory')
        native=author.Native(args.cli.resolve(),folder/'evidence')
        try:temporary,details=create(native,source,part,variant,target,pattern)
        finally:native.close()
        subprocess.run([str(args.checker.resolve()),'--adopt-reference-source',str(temporary),str(part),str(target)],check=True)
        english=illustration_texts(variant,details,'en',labels)
        note_spec=folder/'notes-en.json';note_spec.write_text(json.dumps(english,ensure_ascii=False),encoding='utf-8')
        subprocess.run([str(args.checker.resolve()),'--illustration-texts',str(target),str(note_spec)],check=True)
        for language in LANGUAGES:
            working=folder/(target.stem+'_'+language+'.drwz');shutil.copyfile(target,working)
            note_spec=folder/('notes-'+language+'.json')
            note_spec.write_text(json.dumps(illustration_texts(variant,details,language,labels),ensure_ascii=False),encoding='utf-8')
            subprocess.run([str(args.checker.resolve()),'--illustration-texts',str(working),str(note_spec)],check=True)
            native=author.Native(args.cli.resolve(),folder/('render-'+language))
            try:
                native.run('open',path=str(working));dimensions=native.run('drawing.dimension.list',sheet=details['sheet'])['items']
                assert all(v['state']=='resolved' for v in dimensions)
                image=folder/(target.stem+'_'+language+'.png')
                native.run('export.image',path=str(image),sheet=details['sheet'],dpi=150,crop_mm=[15,15,180,118])
                native.run('close')
            finally:native.close()
        shutil.copyfile(folder/(target.stem+'_en.png'),target.with_suffix('.png'))
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
        assert hashlib.sha256(pattern.read_bytes()).hexdigest()==pattern_hash
        accepted.append(dict(standard=standard,filename=part.name,drawing=target.name,pattern_sha256=pattern_hash,
            source_sha256=digest,dimensions=details['expected'],languages=list(LANGUAGES)))
        (args.output.resolve()/'illustrations.json').write_text(json.dumps(accepted,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('Native pattern illustration and five localized images verified: '+standard,flush=True)


if __name__=='__main__':main()
