"""Exercise coordinate-driven native incidence through the shared command host.

This is a finite matrix, not a claim about every tangent-chain configuration.
"""
import argparse
import hashlib
import importlib.util
import itertools
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('native',ROOT/'tools/generate-fastener-library.py')
api=importlib.util.module_from_spec(spec);spec.loader.exec_module(api)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe');args=parser.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True);report=[]
    (out/'runtime.json').write_text(json.dumps(dict(executable=str(args.cli.resolve()),sha256=hashlib.sha256(args.cli.read_bytes()).hexdigest()),indent=2),encoding='utf-8')
    for kind,axis,sign,locked,driving,full in itertools.product(('line','circle','arc'),(0,1),(-1,1),(False,True),(False,True),(False,True)):
        label=f'{kind}-{axis}-{sign}-{locked}-{driving}-{full}';folder=out/label
        native=api.Native(args.cli.resolve(),folder,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n',keep_going=True);result=dict(case=label,passed=False)
        try:
            native.run('new',type='part',name=label);sk=native.run('sketch.create',name=label)['sketch'];dimensions=[]
            def run(command,**args):return native.run(command,sketch=sk,**args)
            def entity(identifier):return run('sketch.entity.get',entity=identifier)['entity']
            def xy(identifier):
                p=entity(identifier);return [p['x'],p['y']]
            def position(x,y):return [sign*x,y] if axis==0 else [y,sign*x]
            def coordinate(point,index,value,drive=True):
                d=run('sketch.dimension.create',kind='distance_x' if index==0 else 'distance_y',points=[point],geometry=['sketch_axis:'+('y' if index==0 else 'x')],value=value,locked=locked,driving=drive)['dimension']
                dimensions.append((d,point,index));return d
            def make_point(p):return run('sketch.point.create',position=p,construction=True,snap_mm=1e-8)['point']
            if kind=='line':
                curve=run('sketch.segment.create',first=position(2,2),second=position(6,8))['geometry'];line=entity(curve);a,b=line['first'],line['second']
                contact=make_point(position(4,5));run('sketch.constraint.create',kind='point_on_line',points=[contact],geometry=[curve])
                coordinate(a,axis,sign*2);coordinate(a,1-axis,2)
                if full:coordinate(b,1-axis,8)
                coordinate(contact,1-axis,5)
                initial=sign*4
            else:
                if kind=='circle':curve=run('sketch.circle.create',center=position(10,5),radius_mm=3)['geometry']
                else:
                    # The long arc contains the right-hand cardinal contact.
                    curve=run('sketch.arc.create',center=position(10,5),start=position(7,5),end=position(10,2),clockwise=axis==0 and sign==1 or axis==1 and sign==-1,snap_mm=1e-8)['geometry']
                center=entity(curve)['center']
                if kind=='arc':
                    arc=entity(curve)
                    first=min((arc['start'],arc['end']),key=lambda p:math.dist(xy(p),position(7,5)))
                    second=arc['end'] if first==arc['start'] else arc['start']
                    run('sketch.constraint.create',kind='horizontal' if axis==0 else 'vertical',points=[center,first])
                    run('sketch.constraint.create',kind='vertical' if axis==0 else 'horizontal',points=[center,second])
                contact=make_point(position(13,5));run('sketch.constraint.create',kind='point_on_circle',points=[contact],geometry=[curve])
                run('sketch.constraint.create',kind='horizontal' if axis==0 else 'vertical',points=[center,contact])
                radius=run('sketch.dimension.create',kind='radius',geometry=[curve],value=3,locked=locked)['dimension']
                if full:coordinate(center,1-axis,5)
                initial=sign*13
            driver=coordinate(contact,axis,initial,driving)
            def verify():
                status=run('sketch.solve_status');assert status['maximum_residual']<1e-8,status
                assert status['remaining_degrees_of_freedom']==(0 if full else 1)+(0 if driving else 1),status
                p=xy(contact)
                if kind=='line':
                    u,w=xy(a),xy(b);assert abs((p[0]-u[0])*(w[1]-u[1])-(p[1]-u[1])*(w[0]-u[0]))<1e-7,(p,u,w)
                else:
                    c=xy(center);r=run('sketch.dimension.get',dimension=radius)['value']
                    assert abs(math.dist(p,c)-r)<1e-8,(p,c,r)
                    assert abs(p[1-axis]-c[1-axis])<1e-8,('cardinal contact',p,c)
                    if kind=='arc':
                        arc=entity(curve)
                        for end in ('start','end'):assert abs(math.dist(xy(arc[end]),c)-r)<1e-8,('arc radius',xy(arc[end]),c,r)
                for dimension,point,index in dimensions:
                    d=run('sketch.dimension.get',dimension=dimension)
                    assert abs(xy(point)[index]-d['value'])<1e-8,(xy(point),d)
                return p
            verify();edits=0;rejections=0
            for delta in (.1,.2,0):
                before=run('sketch.dimension.get',dimension=driver)['value'];old=verify()
                changed=run('sketch.dimension.set',dimension=driver,value=initial+sign*delta)
                if driving:
                    assert abs(xy(contact)[axis]-(initial+sign*delta))<1e-8
                    native.run('undo');assert abs(run('sketch.dimension.get',dimension=driver)['value']-before)<1e-8;verify()
                    native.run('redo');verify();edits+=1
                else:assert not changed['changed'] and math.dist(old,verify())<1e-9
            for delta in (.1,-.1,.2):
                verify();handle=b if kind=='line' else center
                before=xy(handle);requested=list(before)
                if kind=='line' and not full:
                    anchor=xy(a);requested=[anchor[i]+(1+delta/10)*(before[i]-anchor[i]) for i in (0,1)]
                elif full:requested[axis]+=sign*delta
                else:requested[1-axis]+=delta
                try:run('sketch.point.move',point=handle,position=requested)
                except RuntimeError as error:
                    assert full and locked and driving,'An available native support handle could not move'
                    assert 'constrained_geometry' in str(error),error
                    rejections+=1
                verify();after=xy(handle)
                if full and locked and driving:assert math.dist(after,before)<1e-8,'Locked geometry escaped'
                else:assert math.dist(after,before)>1e-6,('No actual movement',before,after,requested)
            target=folder/(label+'-saved.prtz');native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target));verify();native.run('close')
            result.update(passed=True,successive_driver_edits=edits,three_drags=True,rejected_drags=rejections,undo_redo=driving,save_reopen=True)
        except (RuntimeError,AssertionError,KeyError) as error:result['error']=str(error)
        finally:
            try:native.close()
            except RuntimeError as error:
                if not result['passed']:result['native_exit']=str(error)
                elif not rejections:raise
                # Expected rejected drags mark the CLI process unsuccessful.
        report.append(result);(out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(label+': '+('PASS' if result['passed'] else result['error']),flush=True)
    if not all(r['passed'] for r in report):raise SystemExit(1)


if __name__=='__main__':main()
