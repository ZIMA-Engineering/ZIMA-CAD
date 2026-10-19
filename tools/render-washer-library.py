"""Render dimensioned native washer drawings and five localized illustrations."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('art', ROOT/'tools/render-stud-library.py')
art = importlib.util.module_from_spec(spec); spec.loader.exec_module(art)
LANGUAGES = art.studs.LANGUAGES


def reference(native, v, source, target):
    identifier = next(d['id'] for d in native.run('open', path=str(source)) if d['active'])
    native.run('new', type='drawing', name=target.stem)
    sheet = native.run('drawing.sheet.list')['items'][0]; sid = sheet['sheet']
    w, h = sheet['width_mm'], sheet['height_mm']
    is_tab = 'tab_length_mm' in v
    scale = 64/(v['tab_length_mm']+v['outside_mm']/2) if is_tab else 48/v['outside_mm']
    center_x = (v['outside_mm']/2-v['tab_length_mm'])/2 if is_tab else 0
    top = native.run('drawing.view.create', sheet=sid, source=identifier,
        camera=dict(horizontal=[1,0,0], vertical=[0,-1,0], depth=[0,0,1]),
        x_mm=w-65+center_x*scale, y_mm=h-58, scale=scale, display_style='visible_edges')['view']
    assert sheet['projection']=='first_angle',sheet
    side = native.run('drawing.view.create', sheet=sid, parent_view=top,
        projection_direction='right', distance_mm=75)['view']
    expected = {}; ids = {}
    def circles(view):
        return [r for r in native.run('drawing.view.references', view=view, kind='curve')['items'] if r.get('circle')]
    top_circles = circles(top)
    for owner in (top,side):
        axes=[a for a in native.run('drawing.annotation.list',view=owner,kind='axis')['items']
            if not a['unresolved'] and a['reference']['key'].startswith('axis:profile:')]
        assert axes,'Native washer profile axis missing'
        selected=None
        for rim in sorted(top_circles,key=lambda r:r['circle']['radius_mm'],reverse=True):
            token=':'+rim['key'].rsplit(':',1)[-1]
            selected=next((a for a in axes if a['reference']['key'].endswith(token)),None)
            if selected:break
        assert selected,'Washer axis is not associated with a circular profile'
        native.run('drawing.annotation.show_erase',views=[dict(view=owner,mode='show',
            selection='keep_selected',kinds=['axis'],selected=[selected['reference']])])
        shown=native.run('drawing.annotation.list',view=owner,kind='axis',mode='erase')['items']
        assert len(shown)==1 and shown[0]['visible'] and not shown[0]['unresolved']
    diagonal = (v['outside_mm']/2+10/scale)/math.sqrt(2)
    for symbol, diameter, sign in [('d1',v['bore_mm'],-1),
            ('d2',v['outside_mm'],1)]:
        matches = [r for r in top_circles if abs(2*r['circle']['radius_mm']-diameter)<1e-6]
        assert matches, (symbol,diameter)
        rim = min(matches, key=lambda r:len(r['key']))
        ids[symbol] = native.run('drawing.dimension.create', view=top, kind='diameter',
            attachments=[art.attachment(rim)], style=dict(text_override=symbol),
            placements=[[sign*diagonal,diagonal]])['dimension']
        expected[symbol] = diameter
    side_circles = circles(side)
    cap_rims = []
    for z in (0,v['thickness_mm']):
        matches = [r for r in side_circles if abs(r['circle']['center'][2]-z)<1e-6
            and abs(r['circle']['center'][0])<1e-6 and abs(r['circle']['center'][1])<1e-6]
        assert matches, ('end face',z)
        cap_rims.append(min(matches,key=lambda r:len(r['key'])))
    ids['s'] = native.run('drawing.dimension.create', view=side, kind='linear', direction='horizontal',
        attachments=[art.attachment(r) for r in cap_rims], style=dict(text_override='s'),
        placements=[[v['thickness_mm']/2,-v['outside_mm']/2-8/scale]])['dimension']
    expected['s'] = v['thickness_mm']
    if is_tab:
        points=native.run('drawing.view.references',view=top,kind='point')['items']
        def vertex(x,y):
            matches=[p for p in points if abs(p['position'][0]-x)<1e-6 and
                abs(p['position'][1]-y)<1e-6 and abs(p['position'][2])<1e-6]
            assert matches,(x,y)
            return art.attachment(min(matches,key=lambda p:len(p['key'])),'point')
        l,b=v['tab_length_mm'],v['tab_width_mm']
        ids['b']=native.run('drawing.dimension.create',view=top,kind='linear',direction='vertical',
            attachments=[vertex(-l,-b/2),vertex(-l,b/2)],style=dict(text_override='b'),
            placements=[[-l-7/scale,0]])['dimension'];expected['b']=b
        bore_rim=min([r for r in top_circles if abs(2*r['circle']['radius_mm']-v['bore_mm'])<1e-6],key=lambda r:len(r['key']))
        ids['l']=native.run('drawing.dimension.create',view=top,kind='linear',direction='horizontal',
            attachments=[vertex(-l,b/2),art.attachment(bore_rim)],style=dict(text_override='l'),
            placements=[[-l/2,-v['outside_mm']/2-10/scale]])['dimension'];expected['l']=l
        roots=[r for r in top_circles if abs(r['circle']['radius_mm']-v['root_radius_mm'])<1e-6]
        assert roots
        root=min(roots,key=lambda r:r['circle']['center'][1])
        ids['R']=native.run('drawing.dimension.create',view=top,kind='radius',attachments=[art.attachment(root)],
            style=dict(text_override='R'),placements=[[-l/2,v['outside_mm']/2+9/scale]])['dimension']
        expected['R']=v['root_radius_mm']
    for dimension in native.run('drawing.dimension.list',sheet=sid)['items']:
        assert dimension['state']=='resolved', dimension
        symbol = next(k for k,value in ids.items() if value==dimension['dimension'])
        assert abs(dimension['measurements'][0]['value']-expected[symbol])<1e-6, (symbol,dimension)
    native.run('save_as',path=str(target)); native.run('close',discard=True); native.run('close')
    return sid,expected


def notes(v,lang):
    i=LANGUAGES.index(lang)
    labels=[('d1',('vnitřní průměr','inside diameter','Innendurchmesser','diamètre intérieur','внутренний диаметр')),
        ('d2',('vnější průměr','outside diameter','Außendurchmesser','diamètre extérieur','наружный диаметр')),
        ('s',('tloušťka','thickness','Dicke','épaisseur','толщина'))]
    if 'tab_length_mm' in v:
        labels.extend([
            ('b',('šířka jazýčku','tab width','Lappenbreite','largeur de languette','ширина лапки')),
            ('l',('vzdálenost konce od osy','tab end distance from axis','Abstand des Lappenendes von der Achse','distance du bout à l’axe','расстояние от оси до конца лапки')),
            ('R',('poloměr přechodu','root radius','Übergangsradius','rayon de raccordement','радиус перехода'))])
    texts=[dict(text=v['standard']+' · M'+f'{v["nominal_mm"]:g}'+' · '+v['names'][lang],x_mm=190,y_mm=198,height_mm=2.5)]
    for j,(symbol,values) in enumerate(labels):
        texts.append(dict(text=symbol+' — '+values[i],x_mm=190 if j<3 else 103,
            y_mm=188-5*(j%3),height_mm=2.2))
    if v.get('state')=='compressed':
        text=('Stlačený stav · rovnoběžný řez 45° · mezera 1 mm',
            'Compressed state · parallel 45° cut · gap 1 mm',
            'Zusammengedrückt · paralleler 45°-Schnitt · Spalt 1 mm',
            'État comprimé · coupe parallèle à 45° · jeu 1 mm',
            'Сжатое состояние · параллельный разрез 45° · зазор 1 мм')[i]
        texts.append(dict(text=text,x_mm=190,y_mm=168,height_mm=2.2))
    return dict(locale=lang,texts=texts)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    p.add_argument('--checker',type=Path,default=ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe')
    args=p.parse_args();stage=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants'];receipts=[]
    groups=sorted({(v['standard'],v.get('form','')) for v in rows})
    for standard,form in groups:
        values=[v for v in rows if v['standard']==standard and v.get('form','')==form]
        v=min(values,key=lambda v:abs(v['nominal_mm']-8))
        folder=out/(standard.replace('ČSN','CSN').replace(' ','')+('-'+form if form else ''));folder.mkdir(parents=True,exist_ok=True)
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
        receipts.append(dict(standard=standard,form=form,folder=folder.name,drawing=target.name,part=target.with_suffix('.prtz').name,dimensions=expected))
        (out/'illustrations.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')
        print('Rendered native washer illustration '+standard+' '+form,flush=True)


if __name__=='__main__':main()
