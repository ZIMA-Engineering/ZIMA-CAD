"""Render weld-nut reference drawings with native supports, axes and dimensions."""
import importlib.util
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('renderer',ROOT/'tools/render-hex-nut-library.py')
renderer=importlib.util.module_from_spec(spec);spec.loader.exec_module(renderer)
art=renderer.art


def reference(native,v,source,target):
    identifier=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
    native.run('new',type='drawing',name=target.stem)
    sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet']
    s=v['across_flats_mm'];r=s/math.sqrt(3);iso=v['kind']=='iso'
    outside=v.get('flange_diameter_mm',2*r)/2;scale=42/(2*outside)
    top=native.run('drawing.view.create',sheet=sid,source=identifier,
        camera=dict(horizontal=[1,0,0],vertical=[0,1,0],depth=[0,0,-1]),
        x_mm=sheet['width_mm']-65,y_mm=sheet['height_mm']-58,
        scale=scale,display_style='hidden_edges',show_caption=False)['view']
    assert sheet['projection']=='first_angle'
    side=native.run('drawing.view.create',sheet=sid,parent_view=top,projection_direction='right',distance_mm=75)['view']
    native.run('drawing.view.set',view=side,show_caption=False)
    for view in (top,side):
        axes=[a for a in native.run('drawing.annotation.list',view=view,kind='axis')['items']
            if a['reference']['key']=='axis:primary' and not a['unresolved']]
        assert axes,'Weld-nut native thread axis missing'
        native.run('drawing.annotation.show_erase',views=[dict(view=view,mode='show',
            selection='keep_selected',kinds=['axis'],selected=[a['reference'] for a in axes])])
    points=native.run('drawing.view.references',view=top,kind='point')['items']
    curves={view:native.run('drawing.view.references',view=view,kind='curve')['items'] for view in (top,side)}
    def vertex(x,y,z):
        candidates=[p for p in points if math.dist(p['position'],(x,y,z))<1e-6]
        assert candidates,('Native weld-nut vertex missing',x,y,z)
        return art.attachment(min(candidates,key=lambda p:len(p['key'])),'point')
    def rim(view,z,radius):
        candidates=[p for p in curves[view] if p.get('circle') and
            math.dist(p['circle']['center'],(0,0,z))<1e-6 and
            abs(abs(p['circle']['normal'][2])-1)<1e-6 and abs(p['circle']['radius_mm']-radius)<1e-6]
        assert candidates,('Native weld-nut rim missing',z,radius)
        return art.attachment(min(candidates,key=lambda p:len(p['key'])))
    ids={};expected={}
    def linear(symbol,view,a,b,position,direction,value):
        ids[symbol]=native.run('drawing.dimension.create',view=view,kind='linear',direction=direction,
            attachments=[a,b],style=dict(text_override=symbol),placements=[position])['dimension']
        expected[symbol]=value
    def diameter(symbol,z,radius,position):
        ids[symbol]=native.run('drawing.dimension.create',view=top,kind='diameter',attachments=[rim(top,z,radius)],
            style=dict(text_override=symbol),placements=[position])['dimension']
        expected[symbol]=2*radius
    z=v['projection_height_mm']
    # Use the back bevel's exact remaining corners for the across-flats span.
    end=v.get('overall_height_mm',v['height_mm'])
    corner=end-(r-s/2)*math.tan(math.pi/6)
    linear('s',top,vertex(r/2,-s/2,corner),vertex(r/2,s/2,corner),
        [-outside-8/scale,0],'vertical',s)
    if not iso:
        diameter('d1',v['bore_start_mm'],v['diameter_mm']/2,[outside+8/scale,outside+8/scale])
        diameter('d2',z,v['pilot_diameter_mm']/2,[-outside-8/scale,outside+8/scale])
        contact=vertex(r,0,0);base=vertex(r,0,z)
        linear('m',side,contact,rim(side,end,s/2),[-end/2,-outside-8/scale],'horizontal',end)
        linear('h1',side,contact,base,[-z/2,outside+8/scale],'horizontal',z)
        linear('h2',side,rim(side,v['bore_start_mm'],v['pilot_diameter_mm']/2),
            rim(side,z,v['pilot_diameter_mm']/2),[-z+v['pilot_height_mm']/2,outside+16/scale],
            'horizontal',v['pilot_height_mm'])
    else:
        diameter('D',z+(v['mouth_diameter_mm']-v['diameter_mm'])/2,
            v['diameter_mm']/2,[outside+8/scale,outside+8/scale])
        diameter('dc',z,outside,[-outside-8/scale,outside+8/scale])
        diameter('da',z,v['mouth_diameter_mm']/2,[outside+8/scale,-outside-8/scale])
        base=rim(side,z,outside);back=rim(side,end,s/2)
        linear('m',side,base,back,[-(end+z)/2,-outside-8/scale],'horizontal',v['height_mm'])
        inner=outside-v['projection_depth_mm']
        linear('c',side,rim(side,0,inner),base,[-z/2,outside+8/scale],'horizontal',z)
        linear('f',side,base,rim(side,z+v['flange_height_mm'],outside-v['flange_radius_mm']),
            [-z-v['flange_height_mm']/2,outside+16/scale],'horizontal',v['flange_height_mm'])
        half=v['projection_width_mm']/2;y=-math.sqrt(inner*inner-half*half)
        linear('g',top,vertex(-half,y,z),vertex(half,y,z),[0,-outside-16/scale],
            'horizontal',v['projection_width_mm'])
    for dimension in native.run('drawing.dimension.list',sheet=sid)['items']:
        symbol=next(k for k,value in ids.items() if value==dimension['dimension'])
        assert dimension['state']=='resolved',(symbol,dimension)
        assert abs(dimension['measurements'][0]['value']-expected[symbol])<1e-6,(symbol,dimension)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('close',discard=True)
    return sid,expected


def notes(v,lang):
    i=renderer.LANGUAGES.index(lang);iso=v['kind']=='iso'
    labels=[('D' if iso else 'd1',('jmenovitý průměr závitu','nominal thread diameter','Gewindenenndurchmesser','diamètre nominal du filetage','номинальный диаметр резьбы')),
        ('s',('rozměr přes plochy','width across flats','Schlüsselweite','largeur sur plats','размер под ключ')),
        ('m',('výška bez výstupků' if iso else 'celková výška','height excluding projections' if iso else 'overall height','Höhe ohne Vorsprünge' if iso else 'Gesamthöhe','hauteur sans bossages' if iso else 'hauteur totale','высота без выступов' if iso else 'общая высота')),
        ('c' if iso else 'h1',('výška výstupků','projection height','Vorsprunghöhe','hauteur des bossages','высота выступов'))]
    if iso:
        labels.extend([('dc',('průměr příruby','flange diameter','Flanschdurchmesser','diamètre de l’embase','диаметр фланца')),
            ('da',('průměr ústí otvoru','mouth diameter','Mündungsdurchmesser','diamètre d’entrée','диаметр входа')),
            ('f',('výška příruby','flange height','Flanschhöhe','hauteur de l’embase','высота фланца')),
            ('g',('šířka výstupku','projection width','Vorsprungbreite','largeur du bossage','ширина выступа'))])
    else:
        labels.extend([('d2',('průměr středicího osazení','pilot diameter','Zentrierbunddurchmesser','diamètre du pilote','диаметр центрирующего буртика')),
            ('h2',('výška středicího osazení','pilot height','Zentrierbundhöhe','hauteur du pilote','высота центрирующего буртика'))])
    texts=[dict(text=v['standard']+' · '+v['names'][lang],x_mm=190,y_mm=198,height_mm=2.5)]
    for j,(symbol,values) in enumerate(labels):
        texts.append(dict(text=symbol+' — '+values[i],x_mm=190 if j%2==0 else 103,y_mm=188-5*(j//2),height_mm=2.2))
    placement=('Kontakt výstupků: XY, Z = 0. Model před svařením.',
        'Projection contact: XY, Z = 0. Unwelded model.',
        'Vorsprungkontakt: XY, Z = 0. Ungeschweißtes Modell.',
        'Contact des bossages : XY, Z = 0. Modèle avant soudage.',
        'Контакт выступов: XY, Z = 0. Модель до сварки.')
    texts.append(dict(text=placement[i],x_mm=190,y_mm=168,height_mm=2.2))
    return dict(locale=lang,texts=texts)


if __name__=='__main__':
    renderer.reference=reference;renderer.notes=notes;renderer.main()
