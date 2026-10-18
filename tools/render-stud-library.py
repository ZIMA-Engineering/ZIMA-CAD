"""Render immutable stud thumbnails and dimensioned native reference drawings."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('studs',ROOT/'tools/generate-stud-library.py')
studs=importlib.util.module_from_spec(spec);spec.loader.exec_module(studs)


def attachment(r,kind='center',parameter=0):
    return dict(kind=kind,reference={k:r[k] for k in ('owner','key','instance_path')},parameter=parameter)


def reference(native,v,source,target):
    identifier=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
    native.run('new',type='drawing',name=target.stem)
    sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet']
    scale=130/v['total_length_mm'];left_end=-v['metal_length_mm'];right_end=v['length_mm']
    view=native.run('drawing.view.create',sheet=sid,source=identifier,
        camera=dict(horizontal=[0,0,-1],vertical=[0,1,0],depth=[-1,0,0]),
        x_mm=105+scale*(right_end+left_end)/2,y_mm=242,scale=scale,display_style='hidden_edges')['view']
    curves=native.run('drawing.view.references',view=view,kind='curve')['items']
    circles=[r for r in curves if r.get('circle')]
    def circle(z,thread=False):
        candidates=[r for r in circles if abs(r['circle']['center'][2]-z)<1e-6 and
            (not thread or r['key']=='thread:boundary:root:end')]
        assert candidates,(z,thread)
        return min(candidates,key=lambda r:len(r['key']))
    dims={}
    for symbol,left,right,height in [('e',left_end,0,-9),
            ('b',right_end-v['thread_length_mm'],right_end,-9),
            ('l',0,right_end,-18)]:
        a=circle(-left,thread=symbol in ('l',) or symbol=='b')
        b=circle(-right,thread=symbol=='e')
        dims[symbol]=native.run('drawing.dimension.create',view=view,kind='linear',direction='horizontal',
            attachments=[attachment(a),attachment(b)],style=dict(text_override=symbol),
            placements=[[(left+right)/2,-v['diameter_mm']/2+height/scale]])['dimension']
    rim=next(r for r in circles if abs(r['circle']['radius_mm']-v['diameter_mm']/2)<1e-6)
    dims['d']=native.run('drawing.dimension.create',view=view,kind='linear',direction='vertical',
        attachments=[attachment(rim,'curve_point',.25),attachment(rim,'curve_point',.75)],
        style=dict(text_override='d'),placements=[[right_end+9/scale,0]])['dimension']
    measured=native.run('drawing.dimension.list',view=view)['items']
    expected=dict(e=v['metal_length_mm'],b=v['thread_length_mm'],l=v['length_mm'],d=v['diameter_mm'])
    for d in measured:
        assert d['state']=='resolved',d
        symbol=next(k for k,value in dims.items() if value==d['dimension'])
        assert abs(d['measurements'][0]['value']-expected[symbol])<1e-6,d
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('close')
    return sid,expected


def notes(v,language):
    i=studs.LANGUAGES.index(language)
    labels=dict(l=('jmenovitá délka bez závrtného konce','nominal length excluding metal end','Nennlänge ohne Einschraubende','longueur nominale hors bout implanté','номинальная длина без ввинчиваемого конца'),
        e=('délka závrtného závitu','metal-end thread length','Einschraubgewindelänge','longueur du filetage implanté','длина ввинчиваемой резьбы'),
        b=('délka maticového závitu','nut-end thread length','Muttergewindelänge','longueur du filetage côté écrou','длина резьбы под гайку'),
        d=('jmenovitý průměr závitu','nominal thread diameter','Gewindenenndurchmesser','diamètre nominal du filetage','номинальный диаметр резьбы'))
    texts=[dict(text=v['standard']+' · '+v['designation']+' · '+v['names'][language],x_mm=190,y_mm=201,height_mm=2.6)]
    for j,(key,values) in enumerate(labels.items()):
        texts.append(dict(text=key+' — '+values[i],x_mm=190,y_mm=191-j*5,height_mm=2.4))
    return dict(locale=language,texts=texts)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    p.add_argument('--checker',type=Path,default=ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe')
    p.add_argument('--references-only',action='store_true');args=p.parse_args()
    stage=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants']
    if not args.references_only:
        for v in rows:
            path=stage/v['filename'];digest=hashlib.sha256(path.read_bytes()).hexdigest()
            native=studs.api.Native(args.cli.resolve(),out/'evidence'/path.stem)
            try:
                source=next(d['id'] for d in native.run('open',path=str(path)) if d['active'])
                native.run('new',type='drawing',name='stud-preview');sheet=native.run('drawing.sheet.list')['items'][0]
                camera=dict(horizontal=[1/math.sqrt(6),1/math.sqrt(6),-math.sqrt(2/3)],
                    vertical=[1/math.sqrt(2),-1/math.sqrt(2),0],depth=[1/math.sqrt(3)]*3)
                scale=min(85/(v['total_length_mm']+v['diameter_mm']),55/(1.5*v['diameter_mm']))
                native.run('drawing.view.create',sheet=sheet['sheet'],source=source,camera=camera,
                    x_mm=105-scale*math.sqrt(2/3)*(v['metal_length_mm']-v['length_mm'])/2,y_mm=148.5,scale=scale,display_style='shaded_with_edges')
                native.run('export.image',path=str(out/(path.name+'.png')),sheet=sheet['sheet'],dpi=81.28,crop_mm=[55,111,100,75])
                native.run('close',discard=True);native.run('close')
            finally:native.close()
            assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
    receipts=[]
    for standard in sorted({v['standard'] for v in rows}):
        v=next(v for v in rows if v['standard']==standard and v['diameter_mm']==10 and not v['fine_pitch'])
        folder=out/standard.replace('ČSN','CSN').replace(' ','');folder.mkdir(parents=True,exist_ok=True)
        target=folder/(Path(v['filename']).stem+'-REFERENCE.drwz');assert not target.exists()
        native=studs.api.Native(args.cli.resolve(),folder/'evidence')
        try:sid,expected=reference(native,v,stage/v['filename'],target)
        finally:native.close()
        subprocess.run([str(args.checker.resolve()),'--rebase-reference-drawing',str(target)],check=True)
        for lang in studs.LANGUAGES:
            drawing=folder/(target.stem+'_'+lang+'.drwz');shutil.copyfile(target,drawing)
            spec=folder/('notes-'+lang+'.json');spec.write_text(json.dumps(notes(v,lang),ensure_ascii=False),encoding='utf-8')
            subprocess.run([str(args.checker.resolve()),'--illustration-texts',str(drawing),str(spec)],check=True)
            native=studs.api.Native(args.cli.resolve(),folder/('render-'+lang))
            try:
                native.run('open',path=str(drawing));native.run('export.image',path=str(drawing.with_suffix('.png')),sheet=sid,dpi=150,crop_mm=[15,25,180,105]);native.run('close')
            finally:native.close()
        shutil.copyfile(folder/(target.stem+'_en.png'),target.with_suffix('.png'))
        spec=folder/'notes-en.json'
        subprocess.run([str(args.checker.resolve()),'--illustration-texts',str(target),str(spec)],check=True)
        receipts.append(dict(standard=standard,drawing=target.name,part=target.with_suffix('.prtz').name,dimensions=expected))
        print('Rendered native stud illustration '+standard,flush=True)
    (out/'illustrations.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')


if __name__=='__main__':main()
