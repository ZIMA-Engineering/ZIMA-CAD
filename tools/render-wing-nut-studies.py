"""Render explicitly unfinished wing-nut studies without modifying their source.

Outputs stay under build/ and are not factory-library artwork.
"""
import argparse
from datetime import date
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('wing',ROOT/'tools/generate-wing-nut-library.py')
wing=importlib.util.module_from_spec(spec);spec.loader.exec_module(wing)
spec=importlib.util.spec_from_file_location('preview',ROOT/'tools/render-fastener-library.py')
preview=importlib.util.module_from_spec(spec);spec.loader.exec_module(preview)


def attachment(reference,kind='center',side=1):
    return dict(kind=kind,reference={k:reference[k] for k in ('owner','key','instance_path')},side=side)


def dimensions(native,v,data,front,top):
    def circle(view,radius,axis,center=None):
        candidates=[r for r in data[view]['curve'] if r.get('circle') and
            abs(r['circle']['radius_mm']-radius)<1e-6 and
            abs(r['circle']['normal'][axis])>1-1e-6 and
            (center is None or all(abs(r['circle']['center'][i]-x)<1e-6 for i,x in center.items()))]
        assert candidates,(radius,axis,center)
        return min(candidates,key=lambda r:len(r['key']))
    radius=v['crown_radius_mm'];cx=v['span_mm']/2-radius
    left=circle(front,radius,1,{0:-cx});right=circle(front,radius,1,{0:cx})
    bottom=circle(front,v['diameter_mm']/2,2,{2:0})
    upper=circle(front,v['diameter_mm']/2,2,{2:v['hub_height_mm']})
    expected={}
    def create(label,view,kind,refs,where,value,direction='automatic'):
        result=native.run('drawing.dimension.create',view=view,kind=kind,direction=direction,
            attachments=refs,placements=[where],style=dict(prefix=label+' = ',suffix='',decimals=2))
        expected[result['dimension']]=dict(label=label,value=value,view=view)
    create('e',front,'linear',[attachment(left,'tangent',-1),attachment(right,'tangent',1)],[0,-12],v['span_mm'],'horizontal')
    create('m',front,'linear',[attachment(bottom),attachment(right,'tangent',1)],[-40,16],v['height_mm'],'vertical')
    create('h',front,'linear',[attachment(bottom),attachment(upper)],[44,7],v['hub_height_mm'],'vertical')
    create('R1',front,'radius',[attachment(right)],[37,38],radius)
    create('R4',front,'radius',[attachment(circle(front,v['secondary_edge_radius_mm'],1))],[4,23],v['secondary_edge_radius_mm'])
    for label,radius,z,where in [('Dm',v['hub_base_diameter_mm']/2,0,[21,-18]),
            ('dh',v['hub_top_diameter_mm']/2,v['hub_height_mm'],[22,15]),('d',v['diameter_mm']/2,0,[-24,-18])]:
        create(label,top,'diameter',[attachment(circle(top,radius,2,{2:z}))],where,2*radius)
    def verify():
        measured=native.run('drawing.dimension.list')['items'];assert len(measured)==len(expected)
        for item in measured:
            assert item['state']=='resolved',item
            assert abs(item['measurements'][0]['value']-expected[item['dimension']]['value'])<1e-6,(expected[item['dimension']],item)
    verify()
    return expected,verify


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--models',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    rows=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants'];receipts=[]
    for v in rows:
        source=(args.models/v['filename']).resolve();before=hashlib.sha256(source.read_bytes()).hexdigest()
        native=wing.caps.studs.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',out/'evidence'/source.stem)
        try:
            r=v['hub_base_diameter_mm']/2;e=v['span_mm']/2
            preview.thumbnail(native,source,dict(v,outside_mm=v['span_mm'],thickness_mm=v['height_mm'],bounds_mm=[-e,e,-r,r,0,v['height_mm']]),out/(source.stem+'.png'))
            if v['diameter_mm']==12:
                document=next(d['id'] for d in native.run('open',path=str(source)) if d['active'])
                native.run('new',type='drawing',name='WN-CSN021665-M12-STUDY')
                sheet=native.run('drawing.sheet.list')['items'][0];sid=sheet['sheet'];w,h=sheet['width_mm'],sheet['height_mm']
                native.run('drawing.sheet.set',sheet=sid,locale='en')
                front=native.run('drawing.view.create',sheet=sid,source=document,camera=dict(horizontal=[1,0,0],vertical=[0,0,1],depth=[0,1,0]),x_mm=w-65,y_mm=h-70,scale=1,display_style='hidden_edges',show_caption=False)['view']
                side=native.run('drawing.view.create',sheet=sid,parent_view=front,projection_direction='right',distance_mm=80)['view']
                top=native.run('drawing.view.create',sheet=sid,parent_view=front,projection_direction='bottom',distance_mm=65)['view']
                data={}
                for view in (front,side,top):
                    native.run('drawing.view.set',view=view,show_caption=False)
                    data[view]=dict(annotations=native.run('drawing.annotation.list',view=view)['items'])
                    for kind in ('point','curve'):data[view][kind]=native.run('drawing.view.references',view=view,kind=kind)['items']
                    axes=[a['reference'] for a in data[view]['annotations'] if a['kind']=='axis' and not a['unresolved'] and a['reference']['key']=='axis:primary']
                    if axes:native.run('drawing.annotation.show_erase',views=[dict(view=view,mode='show',selection='keep_selected',kinds=['axis'],selected=axes)])
                (out/'drawing-references.json').write_text(json.dumps(dict(front=front,side=side,top=top,sheet=sid,data=data),indent=2),encoding='utf-8')
                measured,verify=dimensions(native,v,data,front,top)
                native.run('regenerate');verify();drawing=out/'WN-CSN021665-M12-STUDY.drwz';native.run('save_as',path=str(drawing))
                native.run('close',discard=True);native.run('open',path=str(drawing));native.run('regenerate');verify()
                native.run('save');native.run('close')
                subprocess.run([str(ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe'),'--wing-review-title',str(drawing),'en'],check=True)
                notes=out/'study-notes.json';notes.write_text(json.dumps(dict(locale='en',texts=[
                    dict(text='UNRELEASED STUDY - R2 OPEN',x_mm=185,y_mm=195,height_mm=3),
                    dict(text='CSN 02 1665 - M12 - STEEL DENSITY ASSUMED',x_mm=185,y_mm=189,height_mm=2.5)])),encoding='utf-8')
                subprocess.run([str(ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe'),'--illustration-texts',str(drawing),str(notes)],check=True)
                native.run('open',path=str(drawing));verify()
                title=native.run('drawing.title.get',sheet=sid)
                assert title['fields'],'Factory title block fields are missing'
                copy=Path(title['source_path']).resolve()
                assert copy.parent==out and copy!=source,'Review metadata must affect only the owned Save As dependency copy'
                native.run('close');native.run('open',path=str(copy))
                parameters=native.run('document.parameters.get')['parameters']
                for parameter in parameters:
                    if parameter['key'] in ('drawn_by','date'):parameter['values']={'':'ZIMA-Engineering' if parameter['key']=='drawn_by' else date.today().isoformat()}
                native.run('document.parameters.set',parameters=parameters);native.run('save');native.run('close')
                native.run('open',path=str(drawing));native.run('regenerate');verify();native.run('save')
                title=native.run('drawing.title.get',sheet=sid)
                assert any(f['expression']=='&hmotnost' and abs(float(f['value'])-v['saved_mass_kg'])<1e-8 for f in title['fields']),title
                assert any(f['expression']=='&kreslil' and f['value']=='ZIMA-Engineering' for f in title['fields']),title
                (out/'title-block-verification.json').write_text(json.dumps(title,indent=2),encoding='utf-8')
                native.run('export.image',path=str(out/'WN-CSN021665-M12-STUDY.png'),sheet=sid,dpi=130)
                (out/'drawing-verification.json').write_text(json.dumps(dict(dimensions=measured,regenerate=True,save_reopen=True,factory_ready=False,source_copy=str(copy)),indent=2),encoding='utf-8')
                (out/'manifest.json').write_text(json.dumps(dict(variants=[dict(v,filename=copy.name)]),indent=2),encoding='utf-8')
                subprocess.run([str(ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe'),'--wing-studies',str(out)],check=True)
                native.run('close',discard=True);native.run('close')
        finally:native.close()
        assert hashlib.sha256(source.read_bytes()).hexdigest()==before,'Rendering changed the model'
        receipts.append(dict(filename=source.name,source_sha256=before,factory_ready=False))
    (out/'rendering-receipts.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')


if __name__=='__main__':main()
