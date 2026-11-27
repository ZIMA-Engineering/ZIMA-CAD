"""Verify repeated dimension edits, exact mass, Relations and native key readback.

Source candidates are opened read-only on disk. Changed copies and command
receipts remain in the verification directory; source SHA-256 values must match.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('keys',ROOT/'tools/generate-key-library.py')
keys=importlib.util.module_from_spec(spec);spec.loader.exec_module(keys)


def verify_holes(native, row, ids, width, length):
    expected=[(length-width)/2] if row.get('retaining_count')==1 else [width/2,length-1.5*width]
    for hole in ids.get('holes',[]):
        circles=[item for item in native.run('sketch.entities',sketch=hole['sketch'])['items'] if item['kind']=='circle']
        centers=[]
        for circle in circles:
            data=keys.entity(native,hole['sketch'],circle['id'])
            point=keys.entity(native,hole['sketch'],data['center'])
            centers.append(point['x']);assert abs(point['y'])<1e-8,point
            diameter=row['retaining']['key_counterbore_diameter' if hole['counterbore'] else 'key_clearance_diameter']
            assert abs(data['radius']-diameter/2)<1e-8,data
        assert len(centers)==len(expected) and all(abs(a-b)<1e-8 for a,b in zip(sorted(centers),sorted(expected))),centers
        status=native.run('sketch.solve_status',sketch=hole['sketch'])
        assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();stage=args.stage.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    results=[]
    for row in json.loads((stage/'manifest.json').read_text(encoding='utf-8')):
        path=stage/row['filename'];digest=hashlib.sha256(path.read_bytes()).hexdigest()
        ids=json.loads((stage/'evidence'/path.stem/'identities.json').read_text(encoding='utf-8'))
        native=keys.api.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',output/'evidence'/path.stem)
        try:
            native.run('open',path=str(path));b,h,l=row['width'],row['height'],row['length']
            width_dimension=native.run('sketch.dimension.get',sketch=ids['sketch'],dimension=ids['width_dimension'])
            assert width_dimension['kind']==('diameter' if row['woodruff'] else 'distance_y'),width_dimension
            initial=h-l/2 if row['woodruff'] else l
            for delta in (.5,-.25,1.,0.):
                native.run('sketch.dimension.set',sketch=ids['sketch'],dimension=ids['length_dimension'],value=initial+delta)
                status=native.run('sketch.solve_status',sketch=ids['sketch'])
                assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
                native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                verify_holes(native,row,ids,b,l+delta)
                if not row['woodruff']:
                    assert keys.verify_seating_color(native, ids['feature']) == ids['seating_face']
                actual_h=h+delta if row['woodruff'] else h;actual_l=l if row['woodruff'] else l+delta
                if row['woodruff']:
                    r=l/2;chord=math.sqrt(r*r-(r-actual_h)**2)
                    volume=(r*r*math.acos((r-actual_h)/r)-(r-actual_h)*chord)*b
                else:
                    volume=(b*(actual_l-b)+math.pi*b*b/4)*h
                    c=row['edge_chamfer'];volume-=(2*(actual_l-b)+math.pi*b)*c*c-2*math.pi*c*c*c/3
                volume-=keys.retaining_volume(row)
                measured=native.run('document.relations.get')
                assert abs(measured['model_values']['model.volume']-volume)<1e-5,measured
                assert measured['parameters']['stock']==f'{b:g}x{actual_h:g}x{actual_l:g}',measured
                assert abs(measured['model_values']['model.mass']-volume*7800e-9)<1e-10,measured
            if not row['woodruff']:
                for locked in (False, True):
                    for delta in (.25,-.125,.5,0.):
                        native.run('sketch.dimension.set',sketch=ids['sketch'],dimension=ids['width_dimension'],value=b+delta,locked=locked)
                        status=native.run('sketch.solve_status',sketch=ids['sketch'])
                        assert status['remaining_degrees_of_freedom']==0 and status['maximum_residual']<1e-8,status
                        native.run('regenerate');assert not native.run('history.list')['calculation_errors']
                        verify_holes(native,row,ids,b+delta,l)
                        assert keys.verify_seating_color(native, ids['feature']) == ids['seating_face']
                        actual_b=b+delta;c=row['edge_chamfer']
                        volume=(actual_b*(l-actual_b)+math.pi*actual_b**2/4)*h
                        volume-=(2*(l-actual_b)+math.pi*actual_b)*c*c-2*math.pi*c*c*c/3
                        volume-=keys.retaining_volume(row)
                        measured=native.run('document.relations.get')
                        assert abs(measured['model_values']['model.volume']-volume)<1e-5,measured
                        assert measured['parameters']['stock']==f'{actual_b:g}x{h:g}x{l:g}',measured
                native.run('sketch.dimension.set',sketch=ids['sketch'],dimension=ids['width_dimension'],locked=False)
                native.run('appearance.reset');assert not native.run('appearance.get')['groups']
                native.run('undo');keys.verify_seating_color(native,ids['feature'])
                native.run('redo');assert not native.run('appearance.get')['groups']
                native.run('undo');keys.verify_seating_color(native,ids['feature'])
            target=output/path.name
            native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
            native.run('regenerate');assert not native.run('history.list')['calculation_errors']
            verify_holes(native,row,ids,b,l)
            if not row['woodruff']:
                assert keys.verify_seating_color(native, ids['feature']) == ids['seating_face']
            assert native.run('document.relations.get')['parameters']['stock']==f'{b:g}x{h:g}x{l:g}'
            native.run('save');native.run('close')
        finally:native.close()
        assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
        results.append(dict(filename=path.name,source_sha256=digest,repeated_edits=4,
                            width_edits=0 if row['woodruff'] else 8,readback=True,
                            seating_color_verified=not row['woodruff']))
        print('Verified '+path.name,flush=True)
    (output/'verification.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
