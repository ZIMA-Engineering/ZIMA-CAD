"""Render knurled-nut figures from native Parts and real projected views."""
import importlib.util
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('renderer',ROOT/'tools/render-hex-nut-library.py')
renderer=importlib.util.module_from_spec(spec);spec.loader.exec_module(renderer)
art=renderer.art


def reference(native,v,source,target):
    identifier=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
    R=v['outside_mm']/2;scale=42/(2*R);height=v['height_mm'];level=height-v['head_height_mm']
    section=native.run('section.create',plane='XY',path_mm=[[0,-2*R],[0,2*R]],name='A-A',show_cut=True)['object']
    native.run('new',type='drawing',name=target.stem)
    sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet'];w,h=sheet['width_mm'],sheet['height_mm']
    top=native.run('drawing.view.create',sheet=sid,source=identifier,
        camera=dict(horizontal=[1,0,0],vertical=[0,-1,0],depth=[0,0,1]),
        x_mm=w-65,y_mm=h-58,scale=scale,display_style='visible_edges',show_caption=False)['view']
    assert sheet['projection']=='first_angle'
    side=native.run('drawing.view.create',sheet=sid,parent_view=top,projection_direction='right',distance_mm=75)['view']
    native.run('drawing.view.set',view=side,section=section,display_style='visible_edges',show_section_label=False,show_caption=False)
    for owner in (top,side):
        axes=[a for a in native.run('drawing.annotation.list',view=owner,kind='axis')['items']
            if a['reference']['key']=='axis:primary' and not a['unresolved']]
        assert axes,'Native thread axis missing'
        native.run('drawing.annotation.show_erase',views=[dict(view=owner,mode='show',selection='keep_selected',
            kinds=['axis'],selected=[a['reference'] for a in axes])])
    curves={owner:native.run('drawing.view.references',view=owner,kind='curve')['items'] for owner in (top,side)}
    def rim(view,z,radius):
        matches=[p for p in curves[view] if p.get('circle') and
            abs(p['circle']['center'][0])<1e-6 and abs(p['circle']['center'][1])<1e-6 and
            abs(p['circle']['center'][2]-z)<1e-6 and abs(p['circle']['radius_mm']-radius)<1e-6]
        assert matches,('Actual rim missing',view,z,radius)
        return art.attachment(min(matches,key=lambda p:len(p['key'])))
    ids={};expected={}
    for symbol,z,radius,position in [
        ('D',height-v['chamfer_mm'],R,[R+8/scale,R+8/scale]),
        ('d',height,v['diameter_mm']/2,[-R-8/scale,R+8/scale])]:
        ids[symbol]=native.run('drawing.dimension.create',view=top,kind='diameter',attachments=[rim(top,z,radius)],
            style=dict(text_override=symbol),placements=[position])['dimension'];expected[symbol]=2*radius
    def linear(symbol,a,b,position,value):
        ids[symbol]=native.run('drawing.dimension.create',view=side,kind='linear',direction='horizontal',
            attachments=[a,b],style=dict(text_override=symbol),placements=[position])['dimension'];expected[symbol]=value
    linear('h2' if v['low'] else 'h1',rim(side,0,v['hub_diameter_mm']/2),rim(side,height,v['diameter_mm']/2),
        [height/2,-R-8/scale],height)
    linear('k',rim(side,level,R),rim(side,height,v['diameter_mm']/2),[(height+level)/2,R+8/scale],v['head_height_mm'])
    def side_diameter(symbol,z,radius,position):
        support=rim(side,z,radius)
        ids[symbol]=native.run('drawing.dimension.create',view=side,kind='linear',direction='vertical',
            attachments=[dict(support,kind='tangent',side=sign) for sign in (-1,1)],
            style=dict(text_override=symbol),placements=[position])['dimension'];expected[symbol]=2*radius
    side_diameter('D1',0,v['hub_diameter_mm']/2,[-16/scale,0])
    if not v['low']:
        rc=v['clearance_diameter_mm']/2;p=v['clearance_depth_mm']
        linear('p',rim(side,0,rc),rim(side,p,rc),[p/2,R+8/scale],p)
        side_diameter('d3',0,rc,[-8/scale,0])
    for dimension in native.run('drawing.dimension.list',sheet=sid)['items']:
        assert dimension['state']=='resolved',dimension
        symbol=next(k for k,value in ids.items() if value==dimension['dimension'])
        assert abs(dimension['measurements'][0]['value']-expected[symbol])<1e-6,(symbol,dimension)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('close',discard=True)
    return sid,expected


def notes(v,lang):
    i=renderer.LANGUAGES.index(lang)
    text=[dict(text=v['standard']+' · M'+f'{v["diameter_mm"]:g}'+'×'+f'{v["pitch_mm"]:g}'+' · '+v['names'][lang],x_mm=190,y_mm=198,height_mm=2.5)]
    labels=[('d',('jmenovitý průměr závitu','nominal thread diameter','Gewindenenndurchmesser','diamètre nominal du filetage','номинальный диаметр резьбы')),
        ('D',('vnější průměr','outside diameter','Außendurchmesser','diamètre extérieur','наружный диаметр')),
        ('D1',('průměr osazení','hub diameter','Ansatzdurchmesser','diamètre du moyeu','диаметр буртика')),
        ('h2' if v['low'] else 'h1',('celková výška','overall height','Gesamthöhe','hauteur totale','общая высота')),
        ('k',('výška rýhované části','knurled head height','Höhe des Rändelkopfes','hauteur de la tête moletée','высота рифлёной части'))]
    if not v['low']:
        labels.extend([
            ('d3',('průměr vybrání','clearance diameter','Freistichdurchmesser','diamètre du dégagement','диаметр расточки')),
            ('p',('hloubka vybrání','clearance depth','Freistichtiefe','profondeur du dégagement','глубина расточки'))])
    for j,(symbol,values) in enumerate(labels):
        text.append(dict(text=symbol+' — '+values[i],x_mm=190 if j%2==0 else 103,y_mm=188-5*(j//2),height_mm=2.2))
    return dict(locale=lang,texts=text)


if __name__=='__main__':
    renderer.reference=reference;renderer.notes=notes;renderer.main()
