"""Render real formed-nose geometry with associative dimensions in five languages."""
import importlib.util
import configparser
import hashlib
import json
import math
from datetime import date
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('shaped',ROOT/'tools/render-shaped-fastener-library.py')
shaped=importlib.util.module_from_spec(spec);spec.loader.exec_module(shaped)


def reference(native,v,source,target):
    identifier=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
    native.run('new',type='drawing',name=target.stem)
    sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet'];scale=2
    native.run('drawing.sheet.set',sheet=sid,scale=scale)
    v['drawing_sheet_height_mm']=sheet['height_mm']
    top=native.run('drawing.view.create',sheet=sid,source=identifier,
        camera=dict(horizontal=[0,1,0],vertical=[1,0,0],depth=[0,0,1]),
        x_mm=sheet['width_mm']-60,y_mm=sheet['height_mm']-80,scale=scale,
        display_style='visible_edges',show_caption=False)['view']
    side=native.run('drawing.view.create',sheet=sid,parent_view=top,projection_direction='right',distance_mm=75)['view']
    data={}
    for view in (top,side):
        native.run('drawing.view.set',view=view,show_caption=False)
        data[view]={kind:native.run('drawing.view.references',view=view,kind=kind)['items'] for kind in ('point','curve')}
        axes=[a['reference'] for a in native.run('drawing.annotation.list',view=view,kind='axis')['items']
            if not a['unresolved'] and (a['reference']['key']=='axis:primary' or a['reference']['key'].startswith('axis:profile:'))]
        assert axes,'Native formed-nose washer axis missing'
        native.run('drawing.annotation.show_erase',views=[dict(view=view,mode='show',selection='keep_selected',kinds=['axis'],selected=[axes[0]])])
    (target.parent/'native-references.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
    attach=shaped.art.attachment
    def circle(view,radius,axis,center=None):
        matches=[c for c in data[view]['curve'] if c.get('circle') and abs(c['circle']['radius_mm']-radius)<1e-6
            and abs(abs(c['circle']['normal'][axis])-1)<1e-6 and
            (center is None or all(abs(c['circle']['center'][i]-x)<1e-6 for i,x in center.items()))]
        assert matches,('Nose washer exact circle missing',radius,axis,center)
        return attach(min(matches,key=lambda c:len(c['key'])))
    def vertex(view,position):
        matches=[p for p in data[view]['point'] if math.dist(p['position'],position)<1e-6]
        assert matches,('Nose washer exact vertex missing',position)
        return attach(min(matches,key=lambda p:len(p['key'])),'point')
    R=v['outside_mm']/2;t=v['thickness_mm'];c=v['root_distance_mm'];f=v['nose_width_mm'];h=v['height_mm']
    ids={};expected={}
    def dimension(symbol,view,kind,refs,position,value,direction='automatic'):
        ids[symbol]=native.run('drawing.dimension.create',view=view,kind=kind,direction=direction,attachments=refs,
            placements=[position],style=dict(text_override=symbol))['dimension'];expected[symbol]=value
    diagonal=(R+8/scale)/math.sqrt(2)
    dimension('d',top,'diameter',[circle(top,v['bore_mm']/2,2,{2:t})],[-diagonal,-diagonal],v['bore_mm'])
    dimension('D',top,'diameter',[circle(top,R,2,{2:t})],[diagonal,-diagonal],v['outside_mm'])
    dimension('f',top,'linear',[vertex(top,[c+2*t,-f/2,t-h]),vertex(top,[c+2*t,f/2,t-h])],
        [0,c+2*t+8/scale],f,'horizontal')
    dimension('l',top,'linear',[circle(top,v['bore_mm']/2,2,{2:t}),vertex(top,[c,f/2,t])],[-R-8/scale,c/2],c,'vertical')
    dimension('t',side,'linear',[circle(side,v['bore_mm']/2,2,{2:0}),circle(side,v['bore_mm']/2,2,{2:t})],
        [t/2,-R-8/scale],t,'horizontal')
    dimension('h',side,'linear',[vertex(side,[c+2*t,f/2,t-h]),circle(side,v['bore_mm']/2,2,{2:t})],
        [(2*t-h)/2,c+2*t+8/scale],h,'horizontal')
    dimension('r',side,'radius',[circle(side,t,1,{0:c,2:-t})],[-t-10/scale,c+6/scale],t)
    for item in native.run('drawing.dimension.list',sheet=sid)['items']:
        symbol=next(k for k,value in ids.items() if value==item['dimension'])
        assert item['state']=='resolved' and abs(item['measurements'][0]['value']-expected[symbol])<1e-6,(symbol,item)
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('close',discard=True)
    return sid,expected,{}


def notes(v,lang):
    i=shaped.LANGUAGES.index(lang);height=v['drawing_sheet_height_mm']
    labels=[('d / D',('otvor / vnější průměr','bore / outside diameter','Bohrung / Außendurchmesser','alésage / diamètre extérieur','отверстие / наружный диаметр')),
        ('f / l',('šířka nosu / poloha ohybu','nose width / bend position','Nasenbreite / Biegeposition','largeur de l’ergot / position du pli','ширина носика / положение гиба')),
        ('h / t',('celková výška / tloušťka','overall height / thickness','Gesamthöhe / Dicke','hauteur totale / épaisseur','общая высота / толщина')),
        ('r',('zvolený vnitřní poloměr = t','selected inside radius = t','gewählter Innenradius = t','rayon intérieur choisi = t','выбранный внутренний радиус = t'))]
    texts=[dict(text=v['standard']+' · '+v['names'][lang],x_mm=185,y_mm=height-22,height_mm=2.3)]
    for j,(symbol,values) in enumerate(labels):texts.append(dict(text=symbol+' — '+values[i],x_mm=185 if j%2==0 else 100,y_mm=height-28-5*(j//2),height_mm=2.1))
    detail=('Nativní ohyb 90° · r = t · rozvinutí s K = 0,5.',
        'Native 90° Bend · r = t · unfolding with K = 0.5.',
        'Nativer 90°-Bogen · r = t · Abwicklung mit K = 0,5.',
        'Pli natif à 90° · r = t · dépliage avec K = 0,5.',
        'Нативный гиб 90° · r = t · развёртка с K = 0,5.')
    texts.append(dict(text=detail[i],x_mm=185,y_mm=height-42,height_mm=2.1))
    return dict(locale=lang,texts=texts)


def verify_titles(out):
    receipts=[]
    for row in json.loads((out/'illustrations.json').read_text(encoding='utf-8')):
        folder=out/row['folder'];stem=Path(row['drawing']).stem
        stored=configparser.RawConfigParser();stored.read(folder/row['part'],encoding='utf-8')
        expected_mass=float(json.loads(stored['UserParameterValues']['Data'])['mass'][''])
        for language in ('',*shaped.LANGUAGES):
            drawing=folder/(stem+('_'+language if language else '')+'.drwz')
            native=shaped.art.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/('inspect-title-'+(language or 'base')))
            try:
                native.run('open',path=str(drawing));sheet=native.run('drawing.sheet.list')['items'][0]
                title=native.run('drawing.title.get',sheet=sheet['sheet']);fields={f['expression']:f['value'] for f in title['fields']}
                assert expected_mass>0 and abs(float(fields['&hmotnost'].replace(',','.'))-expected_mass)<1e-10,fields
                assert fields['&kreslil']=='ZIMA-Engineering',fields
                date.fromisoformat(fields['&datum'])
                assert fields['&sheet.scale']=='2:1' and sheet['scale']==2,fields
                assert all(view['scale']==2 for view in native.run('drawing.view.list')['items'])
                if not language:native.run('export.image',path=str(out/'full-sheet.png'),sheet=sheet['sheet'],dpi=90,overwrite=True)
                native.run('close')
            finally:native.close()
            receipts.append(dict(drawing=str(drawing.relative_to(out)),source_sha256=hashlib.sha256(drawing.read_bytes()).hexdigest(),locale=language or 'en',
                positive_mass=True,constructor=True,date=True,fields=fields))
    (out/'title-block-verification.json').write_text(json.dumps(receipts,ensure_ascii=False,indent=2),encoding='utf-8')


def main():
    shaped.reference=reference;shaped.notes=notes
    # Keep the drawing number inside the unmodified factory title cells.
    shaped.main(reference_stem=lambda v:'NTW-M'+format(v['nominal_mm'],'02g'))
    out=Path(sys.argv[sys.argv.index('--output')+1]).resolve()
    checker=shaped.pin_checker(out/'title-verification')
    for row in json.loads((out/'illustrations.json').read_text(encoding='utf-8')):
        folder=out/row['folder'];part=folder/row['part'];native=shaped.art.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',folder/'title-source')
        try:
            native.run('open',path=str(part));parameters=native.run('document.parameters.get')['parameters']
            for parameter in parameters:
                if parameter['key'] in ('drawn_by','date'):parameter['values']={'':'ZIMA-Engineering' if parameter['key']=='drawn_by' else date.today().isoformat()}
            native.run('document.parameters.set',parameters=parameters);native.run('save');native.run('close')
        finally:native.close()
        stem=Path(row['drawing']).stem
        for language in ('',*shaped.LANGUAGES):
            drawing=folder/(stem+('_'+language if language else '')+'.drwz')
            subprocess.run([str(checker),'--reference-title',str(drawing),language or 'en'],check=True)
            subprocess.run([str(checker),'--adopt-reference-source',str(drawing),str(part),str(drawing)],check=True)
    verify_titles(out)


if __name__=='__main__':main()
