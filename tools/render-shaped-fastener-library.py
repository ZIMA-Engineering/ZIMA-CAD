"""Create native projected-section drawings of reviewed shaped fasteners."""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import shutil
import subprocess
from fastener_verification_runtime import pin_checker

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('renderer',ROOT/'tools/render-hex-nut-library.py')
renderer=importlib.util.module_from_spec(spec);spec.loader.exec_module(renderer)
art=renderer.art;LANGUAGES=renderer.LANGUAGES


def reference(native,v,source,target):
    identifier=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
    washer=v.get('state')=='compressed';h=v['height_mm']
    outside=v.get('outside_mm',v.get('flange_diameter_mm',2*v.get('across_flats_mm',1)/math.sqrt(3)))/2
    section=native.run('section.create',plane='XY',path_mm=[[-2*outside,0],[2*outside,0]],name='A-A',show_cut=True)['object']
    native.run('new',type='drawing',name=target.stem);sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet'];scale=42/(2*outside)
    top=native.run('drawing.view.create',sheet=sid,source=identifier,camera=dict(horizontal=[0,1,0],vertical=[1,0,0],depth=[0,0,1]),
        x_mm=sheet['width_mm']-65,y_mm=sheet['height_mm']-58,scale=scale,display_style='visible_edges',show_caption=False)['view']
    side=native.run('drawing.view.create',sheet=sid,parent_view=top,projection_direction='right',distance_mm=65)['view']
    native.run('drawing.view.set',view=side,section=section,display_style='visible_edges',show_section_label=False,show_caption=False)
    for view in (top,side):
        axes=[a for a in native.run('drawing.annotation.list',view=view,kind='axis')['items'] if not a['unresolved']]
        assert axes,'Shaped fastener native axis missing'
        primary=next((a for a in axes if a['reference']['key']=='axis:primary'),axes[0])
        native.run('drawing.annotation.show_erase',views=[dict(view=view,mode='show',selection='keep_selected',kinds=['axis'],selected=[primary['reference']])])
    curves={view:native.run('drawing.view.references',view=view,kind='curve')['items'] for view in (top,side)}
    (target.parent/'native-curves.json').write_text(json.dumps(curves,indent=2),encoding='utf-8')
    points=native.run('drawing.view.references',view=top,kind='point')['items'];ids={};expected={};model_dimensions={}
    def rim(view,radius=None,z=None):
        values=[r for r in curves[view] if r.get('circle') and abs(abs(r['circle']['normal'][2])-1)<1e-7
            and math.hypot(*r['circle']['center'][:2])<1e-7 and (radius is None or abs(r['circle']['radius_mm']-radius)<1e-6)
            and (z is None or abs(r['circle']['center'][2]-z)<1e-6)]
        assert values,('Shaped fastener rim missing',radius,z)
        return art.attachment(min(values,key=lambda r:len(r['key'])))
    def linear(symbol,view,a,b,position,direction,value):
        ids[symbol]=native.run('drawing.dimension.create',view=view,kind='linear',direction=direction,attachments=[a,b],style=dict(text_override=symbol),placements=[position])['dimension']
        expected[symbol]=value
    def diameter(symbol,radius,position,z=None):
        ids[symbol]=native.run('drawing.dimension.create',view=top,kind='diameter',attachments=[rim(top,radius,z)],style=dict(text_override=symbol),placements=[position])['dimension']
        expected[symbol]=2*radius
    if not washer:
        s=v['across_flats_mm'];r=s/math.sqrt(3);z=h-(r-s/2)*math.tan(math.radians(v['chamfer_degrees']))
        def vertex(x,y):
            values=[p for p in points if math.dist(p['position'],(x,y,z))<1e-6];assert values,('Shaped nut corner missing',x,y,z)
            return art.attachment(min(values,key=lambda p:len(p['key'])),'point')
        linear('s',top,vertex(r/2,-s/2),vertex(r/2,s/2),[0,outside+8/scale],'horizontal',s)
        diameter('d',v['diameter_mm']/2,[-outside-8/scale,0],h)
    else:
        diameter('d6',v['bore_mm']/2,[-outside-8/scale,0])
        diameter('d8',v['transition_diameter_mm']/2,[0,outside+8/scale],h)
    linear('h',side,rim(side,z=0),rim(side,z=h),[h/2,-outside-8/scale],'horizontal',h)
    if 'flange_diameter_mm' in v or washer:diameter('dc' if not washer else 'd7',outside,[outside+8/scale,0])
    if 'sphere_radius_mm' in v:
        R=v['sphere_radius_mm']
        identity=json.loads((source.parent/'evidence'/source.stem/'identities.json').read_text(encoding='utf-8'))['profile' if washer else 'collar']
        matches=[a for a in native.run('drawing.annotation.list',view=side,kind='dimension')['items'] if not a['unresolved']
            and a['reference']['owner']==identity['sketch'] and a['reference']['key']=='dimension:'+identity['radius']]
        assert len(matches)==1 and abs(matches[0]['value']-R)<1e-8,'Native spherical radius annotation missing'
        ref=matches[0]['reference']
        native.run('drawing.annotation.show_erase',views=[dict(view=side,mode='show',selection='keep_selected',kinds=['dimension'],selected=[ref])])
        native.run('drawing.annotation.set',view=side,reference=ref,layout=dict(radius_center_line_hidden=True,text_outward=8/scale))
        model_dimensions['R']=R
    elif 'end_diameter_mm' in v:diameter('d3',v['end_diameter_mm']/2,[outside+8/scale,0],0)
    for dimension in native.run('drawing.dimension.list',sheet=sid)['items']:
        symbol=next(k for k,key in ids.items() if key==dimension['dimension']);assert dimension['state']=='resolved',(symbol,dimension)
        assert abs(dimension['measurements'][0]['value']-expected[symbol])<1e-6,(symbol,dimension)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('close',discard=True)
    return sid,expected,model_dimensions


def notes(v,lang):
    i=LANGUAGES.index(lang);washer=v.get('state')=='compressed'
    labels=[('h',('celková výška','overall height','Gesamthöhe','hauteur totale','общая высота'))]
    if not washer:labels.extend([('d',('jmenovitý průměr závitu','nominal thread diameter','Gewindenenndurchmesser','diamètre nominal du filetage','номинальный диаметр резьбы')),
        ('s',('rozměr přes plochy','width across flats','Schlüsselweite','largeur sur plats','размер под ключ'))])
    else:labels.extend([('d6',('vnitřní průměr','inside diameter','Innendurchmesser','diamètre intérieur','внутренний диаметр')),
        ('d8',('přechodový průměr','transition diameter','Übergangsdurchmesser','diamètre de transition','переходный диаметр'))])
    if 'sphere_radius_mm' in v:labels.append(('R',('kulový poloměr','spherical radius','Kugelradius','rayon sphérique','сферический радиус')))
    if 'flange_diameter_mm' in v or washer:labels.append(('d7' if washer else 'dc',('vnější průměr nákružku','collar outside diameter','Bundaußendurchmesser','diamètre extérieur de collerette','наружный диаметр буртика')))
    if 'included_angle_degrees' in v:labels.append(('d3',('průměr konce kužele','cone end diameter','Kegelenddurchmesser','diamètre du bout conique','диаметр конца конуса')))
    title=v['standard']+' · '+v['names'][lang]
    if 'included_angle_degrees' in v:title+=f' · {v["included_angle_degrees"]:g}°'
    texts=[dict(text=title,x_mm=190,y_mm=198,height_mm=2.3)]
    for j,(symbol,values) in enumerate(labels):texts.append(dict(text=symbol+' — '+values[i],x_mm=190 if j%2==0 else 103,y_mm=188-5*(j//2),height_mm=2.2))
    if washer:detail=('Stlačený stav · řez 45° · mezera 1 mm','Compressed state · 45° split · gap 1 mm','Zusammengedrückt · 45°-Schnitt · Spalt 1 mm','État comprimé · coupe à 45° · jeu 1 mm','Сжатое состояние · разрез 45° · зазор 1 мм')
    elif 'roof_angle_degrees' in v:detail=('Dosedací ozubení se nemodeluje.','Seating serrations are omitted.','Auflageverzahnung wird nicht modelliert.','La denture d’appui est omise.','Опорные насечки не моделируются.')
    else:detail=('Jmenovitý CAD model · schematický závit','Nominal CAD model · schematic thread','Nennmaß-CAD-Modell · schematisches Gewinde','Modèle CAO nominal · filetage schématique','Номинальная модель САПР · условная резьба')
    texts.append(dict(text=detail[i],x_mm=190,y_mm=168,height_mm=2.2));return dict(locale=lang,texts=texts)


def main(reference_stem=None):
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('stage','output'):p.add_argument('--'+key,type=Path,required=True)
    args=p.parse_args();stage=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants'];receipts=[]
    groups=sorted({(v['standard'],v.get('included_angle_degrees',0)) for v in rows})
    checker=pin_checker(out)
    for standard,angle in groups:
        values=[v for v in rows if v['standard']==standard and v.get('included_angle_degrees',0)==angle]
        v=min(values,key=lambda v:abs(v.get('diameter_mm',v.get('nominal_mm',20))-8))
        folder=out/(standard.replace('ČSN','CSN').replace(' ','').replace(':','-')+(f'-A{angle:g}' if angle else ''));folder.mkdir()
        stem=reference_stem(v) if reference_stem else Path(v['filename']).stem
        target=folder/(stem+'-REFERENCE.drwz');native=art.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'evidence')
        try:sid,expected,model_dimensions=reference(native,v,stage/v['filename'],target)
        finally:native.close()
        subprocess.run([str(checker),'--rebase-reference-drawing',str(target)],check=True)
        for lang in LANGUAGES:
            drawing=folder/(target.stem+'_'+lang+'.drwz');shutil.copyfile(target,drawing)
            spec=folder/('notes-'+lang+'.json');spec.write_text(json.dumps(notes(v,lang),ensure_ascii=False),encoding='utf-8')
            subprocess.run([str(checker),'--illustration-texts',str(drawing),str(spec)],check=True)
            native=art.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/('render-'+lang))
            try:native.run('open',path=str(drawing));native.run('export.image',path=str(drawing.with_suffix('.png')),sheet=sid,dpi=150,crop_mm=[15,15,180,118]);native.run('close')
            finally:native.close()
        shutil.copyfile(folder/(target.stem+'_en.png'),target.with_suffix('.png'))
        subprocess.run([str(checker),'--illustration-texts',str(target),str(folder/'notes-en.json')],check=True)
        receipts.append(dict(standard=standard,folder=folder.name,drawing=target.name,part=target.with_suffix('.prtz').name,dimensions=expected,model_dimensions=model_dimensions,angle_degrees=angle))
        (out/'illustrations.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')
        print('Rendered '+target.name,flush=True)


if __name__=='__main__':main()
