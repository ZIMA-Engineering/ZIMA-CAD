"""Author independent native fastener candidates through the shared C++ command host.

Candidates stay under build until native geometry and reference checks pass.
No native file is edited as text and no user Project is modified.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = ROOT / 'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS/01-HEX-HEAD'
TEMPLATE = LIBRARY / '03-FULL-AB/HHS-ISO4017-M03.0x020.prtz'
SHAFT = '01a14429e3207acba34fc9c038cdcc7f'
HEAD = '01a14429e3207acba34fc9c038cdcc90'
BEARING = '01a14429e3207acba34fc9c038cdcd01'
FILLET = 'c9305d0a-ba0e-4bea-b6a0-9a66ac6ebd2e'
CHAMFER = 'd91bab08-6edd-41cf-ac50-8a156db7f084'
THREAD = '01a14429e3207acba34fc9c038cdccfe'


class Native:
    def __init__(self, executable, evidence, settings_extra=""):
        evidence.mkdir(parents=True, exist_ok=True)
        self.log = (evidence / 'commands.jsonl').open('w', encoding='utf-8')
        self.errors = (evidence / 'stderr.log').open('w', encoding='utf-8')
        settings = evidence / 'authoring.ini'
        settings.write_text('[DocumentNames]\nUppercase=false\nRemoveDiacritics=false\nReplaceSpaces=false\n'+settings_extra, encoding='utf-8')
        self.process = subprocess.Popen([str(executable), '--config', str(settings.resolve()),
            '--working-directory', str(evidence.resolve()), '--stdin'], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=self.errors, text=True, encoding='utf-8', env=os.environ.copy())

    def run(self, command, **arguments):
        request = {'command': command, 'arguments': arguments}
        self.process.stdin.write(json.dumps(request, ensure_ascii=False) + '\n')
        self.process.stdin.flush()
        answer = json.loads(self.process.stdout.readline())
        self.log.write(json.dumps({'request': request, 'response': answer}, ensure_ascii=False) + '\n')
        self.log.flush()
        if not answer['ok']:
            raise RuntimeError(f'{command}: {answer}')
        return answer['data']

    def close(self):
        self.process.stdin.close()
        code = self.process.wait(timeout=60)
        self.log.close()
        self.errors.close()
        if code:
            raise RuntimeError(f'Native CLI exited with code {code}')


def extent(native, container, side, length):
    feature = native.run('feature.get', container=container)
    sides = feature['parameters']['sides']
    sides[side]['length'] = length
    native.run('feature.set', container=container, parameters={'sides': sides})


def author(native, variant, output, template):
    native.run('open', path=str(template))
    native.run('save_as', path=str(output))
    native.run('close')
    native.run('open', path=str(output))
    documents = native.run('documents')
    active = [item for item in documents if item['active']]
    if len(active) != 1 or Path(active[0]['path']).resolve() != output.resolve():
        raise RuntimeError('The independent candidate is not the active document')
    for feature in (FILLET, THREAD, CHAMFER):
        native.run('history.suppress', object=feature, suppressed=True)
    if variant.get('head_chamfer_degrees') is not None:
        native.run('sketch.dimension.set', sketch='01a14429e3207acba34fc9c038cdccd3',
            dimension='01a14429e3207acba34fc9c038cdccf3', value=-variant['head_chamfer_degrees'])
    diameter = variant['diameter_mm']
    for sketch, dimension, value in (
        ('01a14429e3207acba34fc9c038cdcc8f', '01a14429e3207acba34fc9c038cdccb4', variant['across_flats_mm']),
        ('01a14429e3207acba34fc9c038cdcc7e', '01a14429e3207acba34fc9c038cdcc8c', diameter),
        ('01a14429e3207acba34fc9c038cdcd00', '01a14429e3207acba34fc9c038cdcd16', variant['bearing_diameter_mm'])):
        # Continue the authored regular-hexagon solution branch in small edits.
        # A single large nonlinear solve can select a self-intersecting polygon.
        initial = 5.5 if dimension.endswith('ccb4') else (3.0 if dimension.endswith('cc8c') else 4.6)
        steps = max(1, math.ceil(abs(math.log(value / initial)) / math.log(1.1)))
        for step in range(1, steps + 1):
            native.run('sketch.dimension.set', sketch=sketch, dimension=dimension,
                value=initial * (value / initial) ** (step / steps))
    extent(native, HEAD, 1, variant['head_height_mm'])
    extent(native, SHAFT, 0, variant['length_mm'])
    extent(native, BEARING, 1, variant['bearing_height_mm'])
    if not variant.get('bearing_face', variant['product_grade'] != 'C'):
        native.run('history.suppress', object=BEARING, suppressed=True)
    tip = variant['tip_chamfer']
    native.run('chamfer.set', container=CHAMFER, **tip)
    native.run('history.suppress', object=CHAMFER, suppressed=False)
    cones = []
    for reference in native.run('reference.list', kind='face', owner=CHAMFER)['items']:
        surface = native.run('reference.get', kind='face', owner=reference['owner'], key=reference['key'], limit=1)
        if surface.get('surface', {}).get('kind') == 'cone':
            cones.append(reference)
    if len(cones) != 1:
        raise RuntimeError(f'Expected exactly one native tip chamfer, got {len(cones)}')
    arguments = dict(container=THREAD, designation=variant['designation'],
        chamfer={key: cones[0][key] for key in ('owner', 'key')})
    if variant.get('thread_length_mm') is not None:
        arguments.update(end_condition='length', length_mm=variant['thread_length_mm'],
            runout_enabled=True, runout_pitch_factor=variant['runout_mm'] / variant['pitch_mm'])
    native.run('shaft_thread.set', **arguments)
    native.run('history.suppress', object=THREAD, suppressed=False)
    native.run('fillet.set', container=FILLET, radius_mm=variant['underhead_radius_mm'])
    native.run('history.suppress', object=FILLET, suppressed=False)
    if variant.get('locking_hole_diameter_mm'):
        sketch = native.run('sketch.create', name='Pojistný otvor', plane='XZ')['sketch']
        # The established XZ frame has its local Y direction along world -Z.
        hole = native.run('sketch.circle.create', sketch=sketch, center=[0, variant['head_height_mm']/2],
            radius_mm=variant['locking_hole_diameter_mm']/2)['geometry']
        center = native.run('sketch.entity.get', sketch=sketch, entity=hole)['entity']['center']
        native.run('sketch.constraint.create', sketch=sketch, kind='point_on_line',
            points=[center], geometry=['sketch_axis:y'])
        native.run('sketch.dimension.create', sketch=sketch, kind='distance_y',
            points=[center], geometry=['sketch_axis:x'], value=variant['head_height_mm']/2)
        native.run('sketch.dimension.create', sketch=sketch, kind='diameter',
            geometry=[hole], value=variant['locking_hole_diameter_mm'])
        sides = [dict(operation='extrusion', length=variant['across_flats_mm'],
            extrusion_extent='through_all', angle_degrees=90, rotation_extent='angle',
            draft_angle_degrees=0, targets=[]) for _ in range(2)]
        native.run('feature.create', sketch=sketch, combine='subtract',
            parameters=dict(type='modeling', symmetric=False, sides=sides))
    finalize(native, variant, output)


def finalize(native, variant, output):
    native.run('document.material.load', path=str(ROOT /
        f'config/materials/01_steels/fasteners/Steel-{variant["property_class"]}.matz'))
    # Kilograms remain the BOM unit. Six decimal places keep small fasteners'
    # derived mass from rounding to zero; authored dimension annotations retain
    # their independent three-decimal presentation.
    native.run('document.settings.set', precision={'decimal_places': 6})
    parameters = native.run('document.parameters.get')['parameters']
    values = {'standard': {'': variant['standard']}, 'name': variant['names'],
        'material': {'': variant['property_class']}, 'quantity': {'': '-'}}
    for parameter in parameters:
        if parameter['key'] in values:
            parameter['values'] = values[parameter['key']]
        if parameter['key'] == 'mass':
            parameter['labels'] = dict(cs='Hmotnost [kg]',en='Mass [kg]',de='Masse [kg]',
                fr='Masse [kg]',ru='Масса [kg]')
    parameters=[parameter for parameter in parameters if parameter['key']!='product_grade']
    native.run('document.parameters.set', parameters=parameters)
    stock = '"M" & d1 & "x" & d12'
    if variant['fine_pitch']:
        stock = f'"M" & d1 & "x{variant["pitch_mm"]:g}x" & d12'
    native.run('document.relations.set', relations=f'stock = {stock}\nmass = model.mass\n')
    native.run('regenerate')
    native.run('save')
    native.run('close')
    native.run('open', path=str(output))
    native.run('regenerate')
    history = native.run('history.list')
    if history['calculation_errors']:
        raise RuntimeError(str(history['calculation_errors']))
    thread = native.run('shaft_thread.get', container=THREAD)
    if thread['designation'] != variant['designation'] or abs(thread['pitch_mm'] - variant['pitch_mm']) > 1e-9:
        raise RuntimeError('Thread catalog disagrees with the reviewed variant')
    native.run('save')
    native.run('close')


def refine_iso_tip(native,variant,output):
    if not variant['standard'].startswith('ISO '):
        raise RuntimeError('ISO tip refinement cannot change historical variants')
    native.run('open',path=str(output))
    for feature in (FILLET,THREAD):
        native.run('history.suppress',object=feature,suppressed=True)
    native.run('chamfer.set',container=CHAMFER,**variant['tip_chamfer'])
    cones=[]
    for reference in native.run('reference.list',kind='face',owner=CHAMFER)['items']:
        face=native.run('reference.get',kind='face',owner=reference['owner'],key=reference['key'],limit=1)
        if face.get('surface',{}).get('kind')=='cone':cones.append(reference)
    if len(cones)!=1:raise RuntimeError('Refined tip must have one actual cone')
    native.run('shaft_thread.set',container=THREAD,chamfer={key:cones[0][key] for key in ('owner','key')})
    for feature in (THREAD,FILLET):
        native.run('history.suppress',object=feature,suppressed=False)
    finalize(native,variant,output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, default=ROOT / 'build/cpp-windows-release/zima-cad-cli.exe')
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--stage', type=Path, default=ROOT / 'build/fastener-candidates')
    parser.add_argument('--filename', help='Generate one exact manifest filename')
    parser.add_argument('--family', help='Generate one exact manifest family')
    parser.add_argument('--refresh-metadata', action='store_true', help='Update metadata and verify existing staged candidates')
    parser.add_argument('--refine-iso-tip',action='store_true',help='Refine staged ISO chamfered ends below the external thread root')
    args = parser.parse_args()
    args.cli = args.cli.resolve()
    args.stage = args.stage.resolve()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    selected = [v for v in manifest['variants'] if
        (not args.filename or v['filename'] == args.filename) and
        (not args.family or v['family'] == args.family)]
    if not selected:
        parser.error('No matching variants')
    args.stage.mkdir(parents=True, exist_ok=True)
    template_hash = hashlib.sha256(TEMPLATE.read_bytes()).hexdigest()
    template = args.stage / 'evidence/template' / TEMPLATE.name
    template.parent.mkdir(parents=True, exist_ok=True)
    editing=args.refresh_metadata or args.refine_iso_tip
    if not editing:
        shutil.copyfile(TEMPLATE, template)
    for variant in selected:
        output = args.stage / variant['filename']
        if output.exists() and not editing:
            raise RuntimeError(f'Candidate already exists; use a fresh stage: {output}')
        if editing and not output.exists():
            raise RuntimeError(f'Candidate missing: {output}')
        native = Native(args.cli, args.stage / 'evidence' / (output.stem + ('-tip' if args.refine_iso_tip else '-metadata' if args.refresh_metadata else '')))
        try:
            if args.refine_iso_tip:
                refine_iso_tip(native,variant,output)
            elif args.refresh_metadata:
                native.run('open', path=str(output))
                finalize(native, variant, output)
            else:
                author(native, variant, output, template)
            print(f'Calculated and reopened {output.name}', flush=True)
        finally:
            native.close()
            if hashlib.sha256(TEMPLATE.read_bytes()).hexdigest() != template_hash:
                raise RuntimeError('Read-only factory template changed')


if __name__ == '__main__':
    main()
