"""Render distinct CSN cone/flat and DIN hemispherical closed-nut drawings."""
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('renderer',ROOT/'tools/render-hex-nut-library.py')
renderer=importlib.util.module_from_spec(spec);spec.loader.exec_module(renderer)
art=renderer.art


def reference(native,v,source,target):
    identifier=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
    s,h,d=v['across_flats_mm'],v['height_mm'],v['diameter_mm']
    din='hex_height_mm' in v
    r=s/math.sqrt(3);scale=42/(2*r)
    section=native.run('section.create',plane='XY',path_mm=[[0,-2*r],[0,2*r]],name='A-A',show_cut=True)['object']
    native.run('new',type='drawing',name=target.stem)
    sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet']
    top=native.run('drawing.view.create',sheet=sid,source=identifier,
        camera=dict(horizontal=[1,0,0],vertical=[0,1,0],depth=[0,0,-1]),
        x_mm=sheet['width_mm']-65,y_mm=sheet['height_mm']-58,
        scale=scale,display_style='hidden_edges' if din else 'visible_edges',tangent_edge_style='visible',show_caption=False)['view']
    assert sheet['projection']=='first_angle'
    side=native.run('drawing.view.create',sheet=sid,parent_view=top,projection_direction='right',distance_mm=75)['view']
    native.run('drawing.view.set',view=side,section=section,display_style='visible_edges',show_section_label=False,show_caption=False)
    for owner in (top,side):
        axes=[a for a in native.run('drawing.annotation.list',view=owner,kind='axis')['items']
            if a['reference']['key']=='axis:primary' and not a['unresolved']]
        assert axes,'Native cap-nut axis missing'
        native.run('drawing.annotation.show_erase',views=[dict(view=owner,mode='show',selection='keep_selected',
            kinds=['axis'],selected=[a['reference'] for a in axes])])
    curves={owner:native.run('drawing.view.references',view=owner,kind='curve')['items'] for owner in (top,side)}
    if din:(target.parent/'native-curve-supports.json').write_text(json.dumps(curves,indent=2),encoding='utf-8')
    points=native.run('drawing.view.references',view=top,kind='point')['items']
    def vertex(x,y,z):
        matches=[p for p in points if all(abs(a-b)<1e-6 for a,b in zip(p['position'],(x,y,z)))]
        assert matches,('Actual cap-nut vertex missing',x,y,z)
        return art.attachment(min(matches,key=lambda p:len(p['key'])),'point')
    def rim(view,z,radius):
        matches=[p for p in curves[view] if p.get('circle') and
            all(abs(a-b)<1e-6 for a,b in zip(p['circle']['center'],(0,0,z))) and
            abs(abs(p['circle']['normal'][2])-1)<1e-6 and
            abs(p['circle']['radius_mm']-radius)<1e-6]
        assert matches,('Actual cap-nut rim missing',view,z,radius)
        return art.attachment(min(matches,key=lambda p:len(p['key'])))
    ids={};expected={}
    def linear(symbol,owner,a,b,position,direction,value):
        ids[symbol]=native.run('drawing.dimension.create',view=owner,kind='linear',direction=direction,
            attachments=[a,b],style=dict(text_override='(e)' if symbol=='e' else symbol),placements=[position])['dimension']
        expected[symbol]=value
    z=(r-s/2)*math.tan(math.radians(v['chamfer_degrees']))
    linear('s',top,vertex(r/2,-s/2,z),vertex(r/2,s/2,z),[-r-8/scale,0],'vertical',s)
    linear('e',top,vertex(-r,0,z),vertex(r,0,z),[0,-s/2-8/scale],'horizontal',2*r)
    ids['d']=native.run('drawing.dimension.create',view=top,kind='diameter',attachments=[rim(top,0,d/2)],
        style=dict(text_override='d'),placements=[[-r-8/scale,r+8/scale]])['dimension'];expected['d']=d
    mouth=rim(side,0,d/2)
    closed_end=vertex(0,0,h) if din else rim(side,h,v['flat_diameter_mm']/2)
    linear('h',side,mouth,closed_end,[-h/2,-r-8/scale],'horizontal',h)
    for symbol,length,radius,offset in [('t' if din else 'b',v['thread_length_mm'],d/2,8),('p',v['bore_depth_mm'],v['bore_mm']/2,16)]:
        linear(symbol,side,mouth,rim(side,length,radius),[-length/2,r+offset/scale],'horizontal',length)
    if din:
        m=v['hex_height_mm'];R=v['dome_radius_mm'];p=v['bore_depth_mm']
        linear('m',side,rim(side,0,s/2),rim(side,m,s/2),[-m/2,-r-16/scale],'horizontal',m)
        linear('w',side,rim(side,p,v['bore_mm']/2),vertex(0,0,h),[-(h+p)/2,-r-16/scale],'horizontal',h-p)
        ids['dk']=native.run('drawing.dimension.create',view=top,kind='diameter',attachments=[rim(top,h-R,R)],
            style=dict(text_override='dk'),placements=[[r+8/scale,r+8/scale]])['dimension'];expected['dk']=2*R
    else:
        ids['k']=native.run('drawing.dimension.create',view=top,kind='diameter',attachments=[rim(top,h,v['flat_diameter_mm']/2)],
            style=dict(text_override='k'),placements=[[r+8/scale,r+8/scale]])['dimension'];expected['k']=v['flat_diameter_mm']
    for dimension in native.run('drawing.dimension.list',sheet=sid)['items']:
        symbol=next(k for k,value in ids.items() if value==dimension['dimension'])
        assert dimension['state']=='resolved',(symbol,dimension)
        assert abs(dimension['measurements'][0]['value']-expected[symbol])<1e-6,(symbol,dimension)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('close',discard=True)
    return sid,expected


def notes(v,lang):
    i=renderer.LANGUAGES.index(lang)
    din='hex_height_mm' in v
    labels=[('d',('jmenovitý průměr závitu','nominal thread diameter','Gewindenenndurchmesser','diamètre nominal du filetage','номинальный диаметр резьбы')),
        ('s',('rozměr přes plochy','width across flats','Schlüsselweite','largeur sur plats','размер под ключ')),
        ('h',('celková výška','overall height','Gesamthöhe','hauteur totale','общая высота')),
        ('(e)',('skutečný rozměr přes vrcholy','actual width across corners','tatsächliches Eckmaß','largeur réelle sur angles','фактический размер по вершинам')),
        ('t' if din else 'b',('délka závitu','thread length','Gewindelänge','longueur filetée','длина резьбы')),
        ('p',('hloubka otvoru','bore depth','Bohrungstiefe','profondeur du trou','глубина отверстия'))]
    if din:
        labels.extend([
            ('m',('výška šestihranu','hexagon height','Sechskanthöhe','hauteur de l’hexagone','высота шестигранника')),
            ('dk',('průměr vrchlíku','cap diameter','Kappendurchmesser','diamètre de la calotte','диаметр колпачка')),
            ('w',('skutečné dno v ose','actual axial end wall','tatsächlicher axialer Boden','fond axial réel','фактическая осевая толщина дна'))])
    else:
        labels.append(('k',('průměr koncové plošky','end-flat diameter','Durchmesser der Endfläche','diamètre du méplat terminal','диаметр торцевой площадки')))
    text=[dict(text=v['standard']+' · '+v['names'][lang],x_mm=190,y_mm=198,height_mm=2.5)]
    for j,(symbol,values) in enumerate(labels):
        text.append(dict(text=symbol+' — '+values[i],x_mm=190 if j%2==0 else 103,y_mm=188-5*(j//2),height_mm=2.2))
    detail=('ČSN 1970: zakončení 45°, ústí závitu 120°.',
        'CSN 1970: closed end 45°, thread mouth 120°.',
        'ČSN 1970: Abschluss 45°, Gewindemündung 120°.',
        'ČSN 1970 : extrémité 45°, entrée du filetage 120°.',
        'ČSN 1970: закрытый торец 45°, вход резьбы 120°.')
    if not din:text.append(dict(text=detail[i],x_mm=190,y_mm=168,height_mm=2.2))
    return dict(locale=lang,texts=text)


if __name__=='__main__':
    renderer.reference=reference;renderer.notes=notes;renderer.main()
