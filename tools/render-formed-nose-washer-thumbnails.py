"""Render nose-washer thumbnails including their material below the seating plane."""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('preview',ROOT/'tools/render-fastener-library.py')
preview=importlib.util.module_from_spec(spec);spec.loader.exec_module(preview)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--models',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    for v in json.loads((a.models/'manifest.json').read_text(encoding='utf-8'))['variants']:
        b=v['bounds_mm'];camera_case=dict(v,bounds_mm=[b[0],b[3],b[1],b[4],b[2],b[5]])
        image=out/(v['filename']+'.png');native=preview.native_tools.Native(ROOT/'build/cpp-windows-release/zima-cad-cli.exe',out/'evidence'/image.stem)
        try:preview.thumbnail(native,(a.models/v['filename']).resolve(),camera_case,image)
        finally:native.close()
        print('Rendered '+image.name,flush=True)


if __name__=='__main__':main()
