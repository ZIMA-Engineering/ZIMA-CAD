"""Render cylindrical-head native drawings with real attached dimensions."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('caps',ROOT/'tools/generate-cap-screw-library.py')
caps=importlib.util.module_from_spec(spec);spec.loader.exec_module(caps)
spec=importlib.util.spec_from_file_location('art',ROOT/'tools/render-stud-library.py')
art=importlib.util.module_from_spec(spec);spec.loader.exec_module(art)


def reference(native,v,source,target):
    identifier=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
    native.run('new',type='drawing',name=target.stem)
    sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet'];width=sheet['width_mm'];height=sheet['height_mm']
    d,l,k,dk=[v[key] for key in ('diameter_mm','length_mm','head_height_mm','head_diameter_mm')]
    scale=min(100/(l+k),32/dk)
    view=native.run('drawing.view.create',sheet=sid,source=identifier,
        camera=dict(horizontal=[0,0,1],vertical=[0,1,0],depth=[1,0,0]),
        x_mm=width-(30+k*scale),y_mm=height-55,scale=scale,display_style='hidden_edges')['view']
    end=native.run('drawing.view.create',sheet=sid,parent_view=view,projection_direction='right',
        distance_mm=l*scale+22+dk*scale/2,scale=scale,display_style='hidden_edges')['view']
    curves=native.run('drawing.view.references',view=view,kind='curve')['items'];circles=[r for r in curves if r.get('circle')]
    def circle(z,radius=None):
        candidates=[r for r in circles if abs(r['circle']['center'][2]-z)<1e-6 and
            (radius is None or abs(r['circle']['radius_mm']-radius)<1e-6)]
        assert candidates,(z,radius)
        return min(candidates,key=lambda r:len(r['key']))
    dimensions={};expected={}
    def linear(symbol,attachments,position,direction,measured,owner=view):
        dimensions[symbol]=native.run('drawing.dimension.create',view=owner,kind='linear',direction=direction,
            attachments=attachments,style=dict(text_override=symbol),placements=[position])['dimension'];expected[symbol]=measured
    for symbol,a,b,y,value in [('k',-k,0,-dk/2-7/scale,k),('l',0,l,-dk/2-7/scale,l),
            ('b',l-v['thread_length_mm'],l,-dk/2-15/scale,v['thread_length_mm'])]:
        linear(symbol,[art.attachment(circle(a)),art.attachment(circle(b))],[(a+b)/2,y],'horizontal',value)
    for symbol,z,radius,x in [('dk',0,dk/2,-k-8/scale),('d',l,d/2,l+7/scale)]:
        rim=circle(z,radius)
        linear(symbol,[art.attachment(rim,'curve_point',.25),art.attachment(rim,'curve_point',.75)],[x,0],'vertical',2*radius)
    floor=-k+v['drive_depth_mm']
    edges=[r for r in curves if r.get('line') and abs(r['first'][2]-floor)<1e-6 and abs(r['last'][2]-floor)<1e-6]
    assert edges,'Drive floor boundary is missing'
    linear('t',[art.attachment(circle(-k)),art.attachment(edges[0],'curve_point',.5)],[-k+v['drive_depth_mm']/2,dk/2+8/scale],
        'horizontal',v['drive_depth_mm'])
    end_curves=native.run('drawing.view.references',view=end,kind='curve')['items']
    sides=[]
    for y in (-v['drive_width_mm']/2,v['drive_width_mm']/2):
        candidates=[r for r in end_curves if r.get('line') and abs(r['first'][1]-y)<1e-6 and abs(r['last'][1]-y)<1e-6
            and abs(r['first'][2]-floor)<1e-6 and abs(r['last'][2]-floor)<1e-6]
        assert candidates,(y,floor)
        sides.append(min(candidates,key=lambda r:len(r['key'])))
    linear('s' if v['socket'] else 'n',[art.attachment(r,'curve_point',.5) for r in sides],
        [dk/2+7/scale,0],'vertical',v['drive_width_mm'],owner=end)
    for dim in native.run('drawing.dimension.list',sheet=sid)['items']:
        assert dim['state']=='resolved',dim
        symbol=next(k for k,value in dimensions.items() if value==dim['dimension'])
        assert abs(dim['measurements'][0]['value']-expected[symbol])<1e-6,(symbol,dim)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('close')
    return sid,expected


def notes(v,lang):
    labels=json.loads((ROOT/'tools/data/hex-fastener-illustrations.json').read_text(encoding='utf-8'))['legend']
    labels.update(dk=dict(zip(caps.LANGUAGES,('průměr hlavy','head diameter','Kopfdurchmesser','diamètre de tête','диаметр головки'))),
        t=dict(zip(caps.LANGUAGES,('hloubka otvoru nebo drážky','drive depth','Antriebstiefe','profondeur d’empreinte','глубина гнезда или шлица'))),
        n=dict(zip(caps.LANGUAGES,('šířka drážky','slot width','Schlitzbreite','largeur de fente','ширина шлица'))),
        s=dict(zip(caps.LANGUAGES,('vnitřní šestihran přes plochy','hexagon socket across flats','Innensechskant-Schlüsselweite','six pans creux sur plats','размер внутреннего шестигранника под ключ'))))
    text=[dict(text=v['standard']+' · '+v['designation']+' · '+v['names'][lang],x_mm=190,y_mm=198,height_mm=2.5)]
    for i,symbol in enumerate(('l','k','d','dk','b','t','s' if v['socket'] else 'n')):
        text.append(dict(text=symbol+' — '+labels[symbol][lang],x_mm=190 if i%2==0 else 103,
            y_mm=188-5*(i//2),height_mm=2.2))
    return dict(locale=lang,texts=text)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--standard')
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    p.add_argument('--checker',type=Path,default=ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe');args=p.parse_args()
    stage=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants'];receipts=[]
    for standard in sorted({v['standard'] for v in rows}):
        if args.standard and args.standard!=standard:continue
        v=next(v for v in rows if v['standard']==standard and v['diameter_mm']==8)
        folder=out/standard.replace(' ','');folder.mkdir(parents=True,exist_ok=True)
        target=folder/(Path(v['filename']).stem+'-REFERENCE.drwz');assert not target.exists()
        native=caps.studs.api.Native(args.cli.resolve(),folder/'evidence')
        try:sid,expected=reference(native,v,stage/v['filename'],target)
        finally:native.close()
        subprocess.run([str(args.checker.resolve()),'--rebase-reference-drawing',str(target)],check=True)
        for lang in caps.LANGUAGES:
            drawing=folder/(target.stem+'_'+lang+'.drwz');shutil.copyfile(target,drawing)
            spec=folder/('notes-'+lang+'.json');spec.write_text(json.dumps(notes(v,lang),ensure_ascii=False),encoding='utf-8')
            subprocess.run([str(args.checker.resolve()),'--illustration-texts',str(drawing),str(spec)],check=True)
            native=caps.studs.api.Native(args.cli.resolve(),folder/('render-'+lang))
            try:
                native.run('open',path=str(drawing));native.run('export.image',path=str(drawing.with_suffix('.png')),sheet=sid,dpi=150,crop_mm=[15,15,180,118]);native.run('close')
            finally:native.close()
        shutil.copyfile(folder/(target.stem+'_en.png'),target.with_suffix('.png'))
        subprocess.run([str(args.checker.resolve()),'--illustration-texts',str(target),str(folder/'notes-en.json')],check=True)
        receipts.append(dict(standard=standard,drawing=target.name,part=target.with_suffix('.prtz').name,dimensions=expected))
        print('Rendered native cap screw illustration '+standard,flush=True)
    (out/'illustrations.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')


if __name__=='__main__':main()
