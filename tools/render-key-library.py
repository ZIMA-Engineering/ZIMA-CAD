"""Render native key candidates through the shared Drawing image exporter."""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('renderer',ROOT/'tools/render-fastener-library.py')
renderer=importlib.util.module_from_spec(spec);spec.loader.exec_module(renderer)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    args=parser.parse_args();stage=args.stage.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    for row in json.loads((stage/'manifest.json').read_text(encoding='utf-8')):
        b,h,l=row['width'],row['height'],row['length']
        bounds=[-l/2,l/2,-l/2,h-l/2,0,b] if row['woodruff'] else [-b/2,l-b/2,-b/2,b/2,0,h]
        variant=dict(outside_mm=l,thickness_mm=b if row['woodruff'] else h,bounds_mm=bounds,
                     seating_thumbnail=not row['woodruff'] and not row.get('retaining_count'))
        native=renderer.native_tools.Native(args.cli.resolve(),output/'evidence'/Path(row['filename']).stem)
        try:renderer.thumbnail(native,stage/row['filename'],variant,output/(Path(row['filename']).stem+'.png'))
        finally:native.close()
        print('Rendered '+row['filename'],flush=True)

if __name__=='__main__':main()
