"""Render native nut drawings using actual bevel vertices and bore rims."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('art',ROOT/'tools/render-stud-library.py')
art=importlib.util.module_from_spec(spec);spec.loader.exec_module(art)
LANGUAGES=art.studs.LANGUAGES


def reference(native,v,source,target):
    identifier=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
    native.run('new',type='drawing',name=target.stem)
    sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet'];w,h=sheet['width_mm'],sheet['height_mm']
    s,m,d=v['across_flats_mm'],v['height_mm'],v['diameter_mm'];r=s/math.sqrt(3);scale=42/(2*r)
    top=native.run('drawing.view.create',sheet=sid,source=identifier,
        camera=dict(horizontal=[1,0,0],vertical=[0,-1,0],depth=[0,0,1]),
        x_mm=w-65,y_mm=h-58,scale=scale,display_style='visible_edges')['view']
    assert sheet['projection']=='first_angle',sheet
    side=native.run('drawing.view.create',sheet=sid,parent_view=top,
        projection_direction='right',distance_mm=v.get('drawing_projection_distance_mm',75))['view']
    native.run('drawing.view.set',view=side,display_style='hidden_edges')
    for owner in (top,side):
        axes=native.run('drawing.annotation.list',view=owner,kind='axis')['items']
        axes=[a for a in axes if a['reference']['key']=='axis:primary' and not a['unresolved']]
        assert axes,'Native nut thread axis missing'
        native.run('drawing.annotation.show_erase',views=[dict(view=owner,mode='show',
            selection='keep_selected',kinds=['axis'],selected=[a['reference'] for a in axes])])
        shown=native.run('drawing.annotation.list',view=owner,kind='axis',mode='erase')['items']
        assert shown and all(a['visible'] and not a['unresolved'] for a in shown)
    points=native.run('drawing.view.references',view=top,kind='point')['items']
    bevel_height=(r-s/2)*math.tan(math.radians(v['chamfer_degrees']))
    # A turned castle crown removes the upper hex corners. The uncut lower
    # bevel retains the same exact across-flats/corners references.
    corner_z=bevel_height if 'slot_width_mm' in v else m-bevel_height
    def vertex(x,y):
        matches=[p for p in points if abs(p['position'][0]-x)<1e-6 and abs(p['position'][1]-y)<1e-6 and abs(p['position'][2]-corner_z)<1e-6]
        assert matches,('Actual current bevel vertex missing',x,y,corner_z)
        return art.attachment(min(matches,key=lambda p:len(p['key'])),'point')
    ids={};expected={}
    def linear(symbol,owner,attachments,position,direction,value):
        ids[symbol]=native.run('drawing.dimension.create',view=owner,kind='linear',direction=direction,
            attachments=attachments,style=dict(text_override='(e)' if symbol=='e' else symbol),placements=[position])['dimension']
        expected[symbol]=value
    linear('s',top,[vertex(r/2,-s/2),vertex(r/2,s/2)],[r+8/scale,0],'vertical',s)
    linear('e',top,[vertex(-r,0),vertex(r,0)],[0,s/2+8/scale],'horizontal',2*r)
    def rim(view,z,radius):
        matches=[p for p in native.run('drawing.view.references',view=view,kind='curve')['items'] if p.get('circle') and
            abs(p['circle']['center'][0])<1e-6 and abs(p['circle']['center'][1])<1e-6 and
            abs(p['circle']['center'][2]-z)<1e-6 and abs(p['circle']['radius_mm']-radius)<1e-6]
        assert matches,('Bore rim missing',z,radius)
        return min(matches,key=lambda p:len(p['key']))
    ids['d']=native.run('drawing.dimension.create',view=top,kind='diameter',attachments=[art.attachment(rim(top,m,d/2))],
        style=dict(text_override='d'),placements=[[-r-8/scale,0]])['dimension'];expected['d']=d
    total_symbol=v.get('height_symbol','h' if 'slot_width_mm' in v else 'm')
    linear(total_symbol,side,[art.attachment(rim(side,z,d/2)) for z in (0,m)],[m/2,-s/2-8/scale],'horizontal',m)
    if 'slot_width_mm' in v:
        half=v['slot_width_mm']/2
        def slot_lip(x):
            matches=[p for p in points if abs(p['position'][0]-x)<1e-6 and abs(p['position'][2]-m)<1e-6]
            assert matches,('Actual slot lip missing',x,m)
            return art.attachment(min(matches,key=lambda p:p['position'][1]),'point')
        linear('n',top,[slot_lip(-half),slot_lip(half)],[0,-s/2-8/scale],'horizontal',2*half)
        floor=v['slot_floor_mm']
        floor_points=[p for p in points if abs(p['position'][2]-floor)<1e-6]
        if floor_points:
            contact=art.attachment(min(floor_points,key=lambda p:p['position'][1]),'point')
        else:
            roots=[c for c in native.run('drawing.view.references',view=side,kind='curve')['items']
                if c.get('circle') and abs(c['circle']['radius_mm']-half)<1e-6
                and abs(c['circle']['center'][2]-floor-half)<1e-6]
            assert roots,('Actual slot-root support missing',floor,half)
            contact=dict(**art.attachment(roots[0],'tangent'),side=-1)
        linear(v.get('slot_floor_symbol','p'),side,[art.attachment(rim(side,0,d/2)),contact],[floor/2,s/2+8/scale],'horizontal',floor)
        if v.get('crown_relief_mm'):
            relief=v['crown_relief_mm']
            steps=[p for p in points if abs(p['position'][2]-floor+relief)<1e-6]
            assert steps,'Actual crown relief step missing'
            start=art.attachment(min(steps,key=lambda p:p['position'][1]),'point')
            linear('c',side,[start,contact],[floor-relief/2,s/2+16/scale],'horizontal',relief)
        if v['crown_diameter_mm'] and not v.get('crown_diameter_from_flats'):
            ids['D']=native.run('drawing.dimension.create',view=top,kind='diameter',
                attachments=[art.attachment(rim(top,m,v['crown_diameter_mm']/2))],
                style=dict(text_override='D'),placements=[[-r-8/scale,s/2+16/scale]])['dimension']
            expected['D']=v['crown_diameter_mm']
    for dimension in native.run('drawing.dimension.list',sheet=sid)['items']:
        assert dimension['state']=='resolved',dimension
        symbol=next(k for k,value in ids.items() if value==dimension['dimension'])
        assert abs(dimension['measurements'][0]['value']-expected[symbol])<1e-6,(symbol,dimension)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('close')
    return sid,expected


def notes(v,lang):
    i=LANGUAGES.index(lang)
    text=[dict(text=v['standard']+' · M'+f'{v["diameter_mm"]:g}'+'×'+f'{v["pitch_mm"]:g}'+' · '+v['names'][lang],x_mm=190,y_mm=198,height_mm=2.5)]
    labels=[('d',('jmenovitý průměr závitu','nominal thread diameter','Gewindenenndurchmesser','diamètre nominal du filetage','номинальный диаметр резьбы')),
        ('s',('rozměr přes plochy','width across flats','Schlüsselweite','largeur sur plats','размер под ключ')),
        ('m',('výška matice','nut height','Mutternhöhe','hauteur de l’écrou','высота гайки')),
        ('(e)',('skutečný rozměr přes vrcholy','actual width across corners','tatsächliches Eckmaß','largeur réelle sur angles','фактический размер по вершинам'))]
    labels[2]=(v.get('height_symbol','h' if 'slot_width_mm' in v else 'm'),labels[2][1])
    if 'slot_width_mm' in v:
        labels.extend([
            ('n',('šířka drážky','slot width','Schlitzbreite','largeur de la fente','ширина паза')),
            (v.get('slot_floor_symbol','p'),('výška dna drážky','slot floor height','Höhe des Schlitzgrundes','hauteur du fond de la fente','высота дна паза'))])
        if v.get('crown_relief_mm'):
            labels.append(('c',('osové odlehčení koruny','axial crown relief','axialer Kronenfreistich','dégagement axial de la couronne','осевая разгрузка короны')))
        if v['crown_diameter_mm'] and not v.get('crown_diameter_from_flats'):
            labels.append(('D',('průměr koruny','crown diameter','Kronendurchmesser','diamètre de la couronne','диаметр короны')))
    for j,(symbol,values) in enumerate(labels):
        text.append(dict(text=symbol+' — '+values[i],x_mm=190 if j%2==0 else 103,y_mm=188-5*(j//2),height_mm=2.2))
    return dict(locale=lang,texts=text)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--standard');p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    p.add_argument('--checker',type=Path,default=ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe');args=p.parse_args()
    stage=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants'];receipts=[]
    for standard in sorted({v['standard'] for v in rows}):
        if args.standard and args.standard!=standard:continue
        diameter=12 if any('slot_width_mm' in v for v in rows) else 8
        v=next(v for v in rows if v['standard']==standard and v['diameter_mm']==diameter)
        folder=out/standard.replace('ČSN','CSN').replace(' ','');folder.mkdir(parents=True,exist_ok=True)
        target=folder/(Path(v['filename']).stem+'-REFERENCE.drwz');assert not target.exists()
        native=art.studs.api.Native(args.cli.resolve(),folder/'evidence')
        try:sid,expected=reference(native,v,stage/v['filename'],target)
        finally:native.close()
        subprocess.run([str(args.checker.resolve()),'--rebase-reference-drawing',str(target)],check=True)
        for lang in LANGUAGES:
            drawing=folder/(target.stem+'_'+lang+'.drwz');shutil.copyfile(target,drawing)
            spec=folder/('notes-'+lang+'.json');spec.write_text(json.dumps(notes(v,lang),ensure_ascii=False),encoding='utf-8')
            subprocess.run([str(args.checker.resolve()),'--illustration-texts',str(drawing),str(spec)],check=True)
            native=art.studs.api.Native(args.cli.resolve(),folder/('render-'+lang))
            try:
                native.run('open',path=str(drawing));native.run('export.image',path=str(drawing.with_suffix('.png')),sheet=sid,dpi=150,crop_mm=[15,15,180,118]);native.run('close')
            finally:native.close()
        shutil.copyfile(folder/(target.stem+'_en.png'),target.with_suffix('.png'))
        subprocess.run([str(args.checker.resolve()),'--illustration-texts',str(target),str(folder/'notes-en.json')],check=True)
        receipts.append(dict(standard=standard,drawing=target.name,part=target.with_suffix('.prtz').name,dimensions=expected))
        (out/'illustrations.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')
        print('Rendered native nut illustration '+standard,flush=True)


if __name__=='__main__':main()
