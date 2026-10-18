"""Render native fastener thumbnails from calculated Parts without changing them."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_fasteners', ROOT / 'tools/generate-fastener-library.py')
native_tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native_tools)


def thumbnail(native, path, variant, output):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    source = next(item['id'] for item in native.run('open', path=str(path)) if item['active'])
    native.run('new', type='drawing', name='fastener-preview')
    sheet = native.run('drawing.sheet.list')['items'][0]
    width, height = sheet['width_mm'], sheet['height_mm']
    r = variant['head_diameter_mm']/2 if 'head_diameter_mm' in variant else variant['across_flats_mm'] / math.sqrt(3)
    half_flats = r if 'head_diameter_mm' in variant else variant['across_flats_mm'] / 2
    horizontal = math.sqrt(2/3) * (variant['length_mm'] + variant['head_height_mm'] + r + half_flats)
    vertical = math.sqrt(2) * (r + half_flats)
    scale = min(90/horizontal, 65/vertical)
    center_x = math.sqrt(2/3) * (variant['length_mm'] - variant['head_height_mm']) / 2
    camera = {'horizontal':[-1/math.sqrt(6),-1/math.sqrt(6),math.sqrt(2/3)],
        'vertical':[1/math.sqrt(2),-1/math.sqrt(2),0],
        'depth':[-1/math.sqrt(3),-1/math.sqrt(3),-1/math.sqrt(3)]}
    native.run('drawing.view.create', sheet=sheet['sheet'], source=source, camera=camera,
        x_mm=width/2+center_x*scale, y_mm=height/2, scale=scale, display_style='shaded_with_edges')
    native.run('export.image', path=str(output), sheet=sheet['sheet'], dpi=81.28,
        crop_mm=[width/2-50, height/2-37.5, 100, 75])
    native.run('close', discard=True)
    native.run('close')
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError('Thumbnail rendering changed its native source')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, default=ROOT / 'build/cpp-windows-release/zima-cad-cli.exe')
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--stage', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--filename')
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    count = 0
    for variant in manifest['variants']:
        if args.filename and args.filename != variant['filename']:
            continue
        matches = [root.resolve()/variant['filename'] for root in args.stage if (root/variant['filename']).exists()]
        if len(matches) > 1:
            raise RuntimeError(f'Duplicate native candidate: {variant["filename"]}')
        if not matches:
            continue
        output = args.output/(variant['filename']+'.png')
        if output.exists():
            raise RuntimeError(f'Thumbnail already exists: {output}')
        native = native_tools.Native(args.cli.resolve(), args.output/'evidence'/output.stem)
        try:
            thumbnail(native, matches[0], variant, output)
        finally:
            native.close()
        count += 1
        print(f'Rendered {output.name}', flush=True)
    if not count:
        parser.error('No matching native models')
    print(f'{count} native thumbnails rendered at 320 x 240 pixels')


if __name__ == '__main__':
    main()
