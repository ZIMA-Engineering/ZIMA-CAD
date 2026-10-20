"""Render associative lock-nut sections with localized representation notes."""
import importlib.util
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('shaped',ROOT/'tools/render-shaped-fastener-library.py')
shaped=importlib.util.module_from_spec(spec);spec.loader.exec_module(shaped)


def reference(native,v,source,target):
    identifier=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
    s,h,m,d=v['across_flats_mm'],v['height_mm'],v['hex_height_mm'],v['diameter_mm'];r=s/math.sqrt(3)
    section=native.run('section.create',plane='XY',path_mm=[[0,-2*r],[0,2*r]],name='A-A',show_cut=True)['object']
    native.run('new',type='drawing',name=target.stem);sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet'];scale=42/(2*r)
    top=native.run('drawing.view.create',sheet=sid,source=identifier,
        camera=dict(horizontal=[1,0,0],vertical=[0,-1,0],depth=[0,0,1]),x_mm=sheet['width_mm']-65,
        y_mm=sheet['height_mm']-58,scale=scale,display_style='visible_edges',show_caption=False)['view']
    side=native.run('drawing.view.create',sheet=sid,parent_view=top,projection_direction='right',distance_mm=70)['view']
    native.run('drawing.view.set',view=side,section=section,display_style='visible_edges',show_section_label=False,show_caption=False)
    curves={}
    for view in (top,side):
        axes=[a for a in native.run('drawing.annotation.list',view=view,kind='axis')['items'] if not a['unresolved'] and a['reference']['key']=='axis:primary']
        assert axes,'Native locking-nut axis missing'
        native.run('drawing.annotation.show_erase',views=[dict(view=view,mode='show',selection='keep_selected',kinds=['axis'],selected=[axes[0]['reference']])])
        curves[view]=native.run('drawing.view.references',view=view,kind='curve')['items']
    points=native.run('drawing.view.references',view=top,kind='point')['items'];z=m-(r-s/2)*math.tan(math.pi/6)
    def vertex(x,y):
        matches=[p for p in points if math.dist(p['position'],(x,y,z))<1e-6];assert matches,(x,y,z)
        return shaped.art.attachment(min(matches,key=lambda p:len(p['key'])),'point')
    def rim(view,z,radius):
        matches=[c for c in curves[view] if c.get('circle') and math.dist(c['circle']['center'],(0,0,z))<1e-6
            and abs(c['circle']['radius_mm']-radius)<1e-6 and abs(abs(c['circle']['normal'][2])-1)<1e-6]
        assert matches,('Lock nut rim missing',z,radius)
        return shaped.art.attachment(min(matches,key=lambda c:len(c['key'])))
    ids={};expected={}
    def linear(symbol,view,a,b,position,direction,value):
        ids[symbol]=native.run('drawing.dimension.create',view=view,kind='linear',direction=direction,attachments=[a,b],
            placements=[position],style=dict(text_override=symbol))['dimension'];expected[symbol]=value
    linear('s',top,vertex(r/2,-s/2),vertex(r/2,s/2),[r+8/scale,0],'vertical',s)
    linear('(e)',top,vertex(-r,0),vertex(r,0),[0,s/2+8/scale],'horizontal',2*r)
    linear('h',side,rim(side,0,d/2),rim(side,h,v['lip_bore_radius_mm']),[h/2,-s/2-8/scale],'horizontal',h)
    linear('m',side,rim(side,0,d/2),rim(side,m,d/2),[m/2,s/2+8/scale],'horizontal',m)
    for symbol,radius,height,position in [('d',d/2,0,[-r-8/scale,0]),
        ('di',v['insert_bore_radius_mm'],h-v['lip_mm'],[-r-8/scale,-s/2-12/scale])]:
        ids[symbol]=native.run('drawing.dimension.create',view=top,kind='diameter',attachments=[rim(top,height,radius)],
            placements=[position],style=dict(text_override=symbol))['dimension'];expected[symbol]=2*radius
    for item in native.run('drawing.dimension.list',sheet=sid)['items']:
        symbol=next(k for k,value in ids.items() if value==item['dimension'])
        assert item['state']=='resolved' and abs(item['measurements'][0]['value']-expected[symbol])<1e-6,(symbol,item)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('close',discard=True)
    return sid,expected,{}


def notes(v,lang):
    i=shaped.LANGUAGES.index(lang)
    labels=[('d',('jmenovitý průměr závitu','nominal thread diameter','Gewindenenndurchmesser','diamètre nominal du filetage','номинальный диаметр резьбы')),
        ('s',('rozměr přes plochy','width across flats','Schlüsselweite','largeur sur plats','размер под ключ')),
        ('h',('celková výška','overall height','Gesamthöhe','hauteur totale','общая высота')),
        ('m',('zvolená výška kovové části','selected metal height','gewählte Metallhöhe','hauteur métallique choisie','выбранная высота металлической части')),
        ('di',('zvolený otvor symbolické vložky','selected symbolic insert bore','gewählte Bohrung des symbolischen Einsatzes','alésage choisi de l’insert symbolique','выбранное отверстие условной вставки')),
        ('(e)',('skutečný rozměr přes vrcholy','actual width across corners','tatsächliches Eckmaß','largeur réelle sur angles','фактический размер по вершинам'))]
    texts=[dict(text=v['standard']+' · '+v['names'][lang],x_mm=190,y_mm=198,height_mm=2.3)]
    for j,(symbol,values) in enumerate(labels):texts.append(dict(text=symbol+' — '+values[i],x_mm=190 if j%2==0 else 103,y_mm=188-5*(j//2),height_mm=2.1))
    detail=('Jeden ocelový Part · vložka symbolicky barvou · hmotnost celá z oceli.',
        'One steel Part · insert indicated by color · all volume uses steel density.',
        'Ein Stahl-Part · Einsatz farblich angedeutet · gesamte Masse als Stahl.',
        'Un Part en acier · insert indiqué par sa couleur · toute la masse en acier.',
        'Один стальной Part · вставка обозначена цветом · весь объём считается сталью.')
    texts.append(dict(text=detail[i],x_mm=190,y_mm=165,height_mm=2.1))
    return dict(locale=lang,texts=texts)


if __name__=='__main__':
    shaped.reference=reference;shaped.notes=notes;shaped.main()
