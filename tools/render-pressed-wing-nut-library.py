"""Render associative drawings and localized notes for pressed wing nuts."""
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pressed',ROOT/'tools/generate-pressed-wing-nut-library.py')
pressed=importlib.util.module_from_spec(spec);spec.loader.exec_module(pressed)
spec=importlib.util.spec_from_file_location('shaped',ROOT/'tools/render-shaped-fastener-library.py')
shaped=importlib.util.module_from_spec(spec);spec.loader.exec_module(shaped)


def reference(native,v,source,target):
    identifier=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
    native.run('new',type='drawing',name=target.stem)
    sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet'];scale=42/v['span_mm']
    v['drawing_sheet_height_mm']=sheet['height_mm']
    front=native.run('drawing.view.create',sheet=sid,source=identifier,
        camera=dict(horizontal=[1,0,0],vertical=[0,0,1],depth=[0,1,0]),
        x_mm=sheet['width_mm']-60,y_mm=sheet['height_mm']-72,scale=scale,
        display_style='visible_edges',show_caption=False)['view']
    side=native.run('drawing.view.create',sheet=sid,parent_view=front,projection_direction='right',distance_mm=75)['view']
    top=native.run('drawing.view.create',sheet=sid,parent_view=front,projection_direction='bottom',distance_mm=35)['view']
    data={}
    for view in (front,side,top):
        native.run('drawing.view.set',view=view,show_caption=False)
        data[view]={k:native.run('drawing.view.references',view=view,kind=k)['items'] for k in ('point','curve')}
        axes=[a['reference'] for a in native.run('drawing.annotation.list',view=view,kind='axis')['items']
            if not a['unresolved'] and a['reference']['key']=='axis:primary']
        assert axes,'Pressed wing native axis missing'
        native.run('drawing.annotation.show_erase',views=[dict(view=view,mode='show',selection='keep_selected',kinds=['axis'],selected=axes)])
    (target.parent/'native-references.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
    def circle(view,radius,axis,center=None):
        matches=[r for r in data[view]['curve'] if r.get('circle') and
            abs(r['circle']['radius_mm']-radius)<1e-6 and abs(abs(r['circle']['normal'][axis])-1)<1e-6 and
            (center is None or all(abs(r['circle']['center'][i]-x)<1e-6 for i,x in center.items()))]
        assert matches,('Pressed wing circle missing',radius,axis,center)
        return min(matches,key=lambda r:len(r['key']))
    attach=shaped.art.attachment
    radius=v['outside_radius_mm'];g=pressed.profile_geometry(v)
    left=circle(front,radius,1,{0:-g['c'][0]});right=circle(front,radius,1,{0:g['c'][0]})
    bottom=circle(front,v['diameter_mm']/2,2,{2:0})
    upper=circle(front,v['diameter_mm']/2,2,{2:v['hub_height_mm']})
    high=[p for p in data[front]['point'] if abs(p['position'][2]-v['height_mm'])<1e-6]
    assert high,'Pressed wing highest native vertex missing'
    ids={};expected={}
    def create(symbol,view,kind,refs,where,value,direction='automatic'):
        ids[symbol]=native.run('drawing.dimension.create',view=view,kind=kind,direction=direction,
            attachments=refs,placements=[where],style=dict(text_override=symbol))['dimension'];expected[symbol]=value
    create('e',front,'linear',[dict(attach(left,'tangent'),side=-1),dict(attach(right,'tangent'),side=1)],
        [0,-8/scale],v['span_mm'],'horizontal')
    create('h',front,'linear',[attach(bottom),attach(high[0],'point')],[-v['span_mm']/2-8/scale,v['height_mm']/2],v['height_mm'],'vertical')
    create('p',front,'linear',[attach(bottom),attach(upper)],[v['span_mm']/2+8/scale,v['hub_height_mm']/2],v['hub_height_mm'],'vertical')
    create('R1',front,'radius',[attach(right)],[v['span_mm']/2+7/scale,v['height_mm']+7/scale],radius)
    for symbol,r,z,where in [('D',v['hub_base_diameter_mm']/2,0,[0,-v['hub_base_diameter_mm']/2-7/scale]),
        ('d1',v['hub_top_diameter_mm']/2,v['hub_height_mm'],[0,v['hub_base_diameter_mm']/2+7/scale]),
        ('d',v['diameter_mm']/2,0,[-v['span_mm']/2-8/scale,0])]:
        create(symbol,top,'diameter',[attach(circle(top,r,2,{2:z}))],where,2*r)
    identity=json.loads((source.parent/'evidence'/source.stem/'identities.json').read_text())['hub']
    radius_annotations=[a for a in native.run('drawing.annotation.list',view=side,kind='dimension')['items']
        if not a['unresolved'] and a['reference']['owner']==identity['sketch'] and a['reference']['key']=='dimension:'+identity['dimensions']['radius']]
    assert len(radius_annotations)==1 and abs(radius_annotations[0]['value']-v['hub_radius_mm'])<1e-8
    ref=radius_annotations[0]['reference']
    native.run('drawing.annotation.show_erase',views=[dict(view=side,mode='show',selection='keep_selected',kinds=['dimension'],selected=[ref])])
    native.run('drawing.annotation.set',view=side,reference=ref,layout=dict(radius_center_line_hidden=True,text_outward=8/scale))
    for item in native.run('drawing.dimension.list',sheet=sid)['items']:
        symbol=next(k for k,key in ids.items() if key==item['dimension'])
        assert item['state']=='resolved' and abs(item['measurements'][0]['value']-expected[symbol])<1e-6,(symbol,item)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('close',discard=True)
    return sid,expected,dict(R2=v['hub_radius_mm'])


def notes(v,lang):
    i=shaped.LANGUAGES.index(lang);height=v['drawing_sheet_height_mm']
    labels=[('d',('jmenovitý závit','nominal thread','Nenngewinde','filetage nominal','номинальная резьба')),
        ('e',('rozpětí křídel','wing span','Flügelspannweite','envergure des oreilles','размах крыльев')),
        ('h / p',('celková výška / výška náboje','overall / hub height','Gesamt- / Nabenhöhe','hauteur totale / du moyeu','общая высота / высота ступицы')),
        ('D / d1',('dolní / horní průměr náboje','lower / upper hub diameter','unterer / oberer Nabendurchmesser','diamètre inférieur / supérieur du moyeu','нижний / верхний диаметр ступицы'))]
    texts=[dict(text=v['standard']+' · '+v['names'][lang],x_mm=190,y_mm=height-22,height_mm=2.3)]
    for j,(symbol,values) in enumerate(labels):
        texts.append(dict(text=symbol+' — '+values[i],x_mm=190 if j%2==0 else 103,y_mm=height-28-5*(j//2),height_mm=2.1))
    detail=('Jmenovitý CAD tvar · nezadané úhly 70° · schematický závit.',
        'Nominal CAD shape · unspecified angles selected as 70° · schematic thread.',
        'Nennmaß-CAD-Form · nicht angegebene Winkel mit 70° gewählt · schematisches Gewinde.',
        'Forme CAO nominale · angles non spécifiés choisis à 70° · filetage schématique.',
        'Номинальная форма САПР · неуказанные углы приняты 70° · условная резьба.')
    texts.append(dict(text=detail[i],x_mm=190,y_mm=height-42,height_mm=2.1))
    return dict(locale=lang,texts=texts)


if __name__=='__main__':
    shaped.reference=reference;shaped.notes=notes;shaped.main()
