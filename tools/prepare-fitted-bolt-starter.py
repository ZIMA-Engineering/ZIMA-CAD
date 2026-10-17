"""Author an editable M10 fitted-bolt starting Part through current native commands.

This is a modeling starting point, not a finished standards-compliant factory bolt.
The user completes the thread, its runout, fillets and manufacturing details.
"""
import argparse
import copy
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fasteners', ROOT / 'tools/generate-fastener-library.py')
author = importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, default=ROOT / 'build/cpp-windows-release/zima-cad-cli.exe')
    parser.add_argument('--manifest', type=Path, default=ROOT / 'build/hex-fastener-followup/manifest.json')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise RuntimeError(f'Refusing to overwrite an existing Part: {output}')
    data = json.loads(args.manifest.read_text(encoding='utf-8'))
    variant = copy.deepcopy(next(v for v in data['variants']
        if v['standard'] == 'ISO 4014' and v['diameter_mm'] == 10))
    variant.update(standard='ČSN 02 1111', length_mm=50, head_height_mm=7,
        across_flats_mm=16, underhead_radius_mm=1, bearing_face=False,
        thread_length_mm=16, runout_mm=3, minimum_unthreaded_shank_mm=31,
        filename=output.name, names=dict(cs='Lícovaný šroub – základ k domodelování',
            en='Fitted bolt – editable starting model', de='Passschraube – bearbeitbares Ausgangsmodell',
            fr='Boulon ajusté – modèle de départ modifiable', ru='Призонный болт — исходная редактируемая модель'))
    native = author.Native(args.cli.resolve(), output.parent / 'evidence' / output.stem)
    try:
        author.author(native, variant, output, author.TEMPLATE)
        native.run('open', path=str(output))
        # Retain only the editable ordinary shaft, hexagonal head and head bevel.
        for container in (author.FILLET, author.THREAD, author.CHAMFER, author.BEARING):
            native.run('history.delete', object=container)
        sketch = native.run('sketch.create', name='Lícovaný dřík', plane='XY')['sketch']
        circle = native.run('sketch.circle.create', sketch=sketch, center=[0, 0], radius_mm=5.5)['geometry']
        center = native.run('sketch.entity.get', sketch=sketch, entity=circle)['entity']['center']
        native.run('sketch.constraint.create', sketch=sketch, kind='point_reference', points=[center, 'sketch_origin'])
        native.run('sketch.dimension.create', sketch=sketch, kind='diameter', geometry=[circle], value=11)
        # The underlying Ø10 blank continues to 50 mm. The concentric Ø11 Add
        # defines the fitted region without a display-only diameter or epsilon.
        sides = [dict(operation='extrusion', length=34, extrusion_extent='length',
            angle_degrees=90, rotation_extent='angle', draft_angle_degrees=0, targets=[]),
            dict(operation='none', length=50, extrusion_extent='length',
            angle_degrees=90, rotation_extent='angle', draft_angle_degrees=0, targets=[])]
        native.run('feature.create', sketch=sketch, combine='add', name='Lícovaný dřík',
            parameters=dict(type='modeling', symmetric=False, sides=sides))
        native.run('regenerate')
        native.run('save')
        native.run('close')
        native.run('open', path=str(output))
        history = native.run('history.list')
        if history['calculation_errors']:
            raise RuntimeError(str(history['calculation_errors']))
        native.run('close')
    finally:
        native.close()
    print(output)


if __name__ == '__main__':
    main()
