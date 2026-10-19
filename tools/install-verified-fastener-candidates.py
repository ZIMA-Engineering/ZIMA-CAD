"""Install new reviewed socket and washer families after all asset gates pass.

Existing different files are rejected. Metadata is merged without discarding
user-authored fields. Source documents and evidence remain outside the library.
"""
import argparse
import configparser
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'config/lib/03-ENGINEERING-TABLES/05-FASTENERS'
LANGUAGES=('cs','en','de','fr','ru')
FIELDS=('name','standard','stock','material','quantity','mass')
spec=importlib.util.spec_from_file_location('installer',ROOT/'tools/install-fastener-library.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    result=configparser.RawConfigParser();result.optionxform=str
    result.read(path,encoding='utf-8');return result


def destination(kind,v):
    if kind in ('hex-nuts','small-hex-nuts','coupling-nuts','castle-nuts','small-castle-nuts','knurled-nuts','din-cap-nuts','csn-closed-nuts','din-weld','iso-weld'):return BASE/'05-NUTS'/v['family']
    if kind in ('special','caps'):return BASE/'04-SCREWS'/('06-PAN-HEAD' if v['family']=='04-BUTTON-A2' else '04-CAP-HEAD')/v['family']
    return BASE/'06-WASHERS'/('03-SPRING' if kind=='spring' else v['family'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kind',choices=('special','caps','spring','plain','iso-plain','hex-nuts','small-hex-nuts','coupling-nuts','castle-nuts','small-castle-nuts','knurled-nuts','din-cap-nuts','csn-closed-nuts','din-weld','iso-weld','tab'),required=True)
    p.add_argument('--gui-verification',type=Path,help='Independent representative GUI edit receipts, required for knurled nuts')
    p.add_argument('--catalog-verification',type=Path,help='Native thread-catalog edit receipts, required for knurled nuts')
    p.add_argument('--drawing-verification',type=Path,help='Regeneration and reopen receipts for every staged native drawing')
    p.add_argument('--replace-receipt',type=Path,help='Replace only unchanged assets from this previous installation receipt')
    for name in ('models','verification','thumbnails','references'):p.add_argument('--'+name,type=Path,required=True)
    args=p.parse_args();rows=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']
    if args.kind=='special':rows=[v for v in rows if v['standard'] in ('DIN 7984','DIN 6912','ISO 14579')]
    assert len(rows)=={'special':25,'caps':72,'spring':41,'plain':127,'iso-plain':134,'hex-nuts':166,'small-hex-nuts':15,'coupling-nuts':14,'castle-nuts':53,'small-castle-nuts':15,'knurled-nuts':22,'din-cap-nuts':26,'csn-closed-nuts':11,'din-weld':16,'iso-weld':14,'tab':19}[args.kind]
    cases={v['filename']:v for v in json.loads(args.verification.read_text(encoding='utf-8'))}
    refs=json.loads((args.references/'illustrations.json').read_text(encoding='utf-8'))
    refs=[v for v in refs if v['standard'] in {r['standard'] for r in rows}]
    if args.kind=='coupling-nuts':
        assert args.gui_verification and args.drawing_verification
        gui=json.loads(args.gui_verification.read_text(encoding='utf-8'))
        assert {r['filename'] for r in gui}=={'CPN-DIN6334-M08.0.prtz','CPN-DIN6334-M36.0.prtz'}
        for r in gui:
            assert r['source_sha256']==digest(args.models/r['filename']) and r['mouse_dimension_edits']==3
            assert all(r[k] for k in ('independent_geometry','undo_redo','save_reopen','regenerate'))
            assert digest(Path(r['output']))==r['output_sha256']
        proof=json.loads(args.drawing_verification.read_text(encoding='utf-8'))
        expected={p.relative_to(args.references).as_posix() for p in args.references.rglob('*.drwz')}
        assert {r['drawing'].replace('\\','/') for r in proof}==expected and len(expected)==6
        for r in proof:
            assert r['dimensions']==4 and all(r[k] for k in ('regenerate','save_reopen','identities_retained'))
            assert digest(args.references/r['drawing'])==r['source_sha256']
            assert digest(args.references/r['part'])==r['part_sha256']
    assert len(refs)=={'special':3,'caps':7,'spring':2,'plain':4,'iso-plain':7,'hex-nuts':8,'small-hex-nuts':1,'coupling-nuts':1,'castle-nuts':2,'small-castle-nuts':1,'knurled-nuts':2,'din-cap-nuts':1,'csn-closed-nuts':1,'din-weld':1,'iso-weld':1,'tab':1}[args.kind]
    if args.kind in ('din-weld','iso-weld'):
        assert args.gui_verification and args.drawing_verification
        prefix='HWN-DIN929' if args.kind=='din-weld' else 'HFWN-ISO21670'
        gui=json.loads(args.gui_verification.read_text(encoding='utf-8'))
        assert {r['filename'] for r in gui}=={prefix+'-M08.0.prtz',prefix+'-M08.0x1.00.prtz'}
        for r in gui:
            assert r['source_sha256']==digest(args.models/r['filename'])
            assert r['mouse_dimension_edits']==(3 if args.kind=='din-weld' else 6)
            assert all(r[k] for k in ('independent_geometry','retained_external_supports','undo_redo','save_reopen','regenerate'))
        proof=json.loads(args.drawing_verification.read_text(encoding='utf-8'))
        expected={p.relative_to(args.references).as_posix() for p in args.references.rglob('*.drwz')}
        assert {r['drawing'].replace('\\','/') for r in proof}==expected and len(expected)==6
        for r in proof:
            assert r['dimensions']==(6 if args.kind=='din-weld' else 8)
            assert all(r[k] for k in ('regenerate','save_reopen','identities_retained'))
            assert digest(args.references/r['drawing'])==r['source_sha256']
            assert digest(args.references/r['part'])==r['part_sha256']
    if args.kind=='din-cap-nuts':
        assert args.gui_verification and args.drawing_verification
        gui=json.loads(args.gui_verification.read_text(encoding='utf-8'))
        assert {r['filename'] for r in gui}=={'CN-DIN1587-M08.0.prtz','CN-DIN1587-M10.0x1.00.prtz'}
        for r in gui:
            assert r['source_sha256']==digest(args.models/r['filename'])
            assert r['mouse_radius_edits']==3 and r['properties_thread_edits']==3
            assert all(r[k] for k in ('independent_geometry','undo_redo','save_reopen','regenerate'))
            assert digest(Path(r['output']))==r['output_sha256']
        proof=json.loads(args.drawing_verification.read_text(encoding='utf-8'))
        expected={p.relative_to(args.references).as_posix() for p in args.references.rglob('*.drwz')}
        assert {r['drawing'].replace('\\','/') for r in proof}==expected and len(expected)==6
        for r in proof:
            assert r['dimensions']==9 and all(r[k] for k in ('regenerate','save_reopen','identities_retained'))
            assert digest(args.references/r['drawing'])==r['source_sha256']
            assert digest(args.references/r['part'])==r['part_sha256']
    if args.kind=='csn-closed-nuts':
        assert args.gui_verification and args.drawing_verification
        gui=json.loads(args.gui_verification.read_text(encoding='utf-8'))
        assert {r['filename'] for r in gui}=={'CN-CSN021431-M08.0.prtz','CN-CSN021431-M30.0.prtz'}
        for r in gui:
            assert r['source_sha256']==digest(args.models/r['filename'])
            assert r['mouse_dimension_edits']==6 and r['mouse_point_drags']==3
            assert all(r[k] for k in ('independent_geometry','undo_redo','save_reopen','regenerate'))
            assert digest(Path(r['output']))==r['output_sha256']
        proof=json.loads(args.drawing_verification.read_text(encoding='utf-8'))
        expected={p.relative_to(args.references).as_posix() for p in args.references.rglob('*.drwz')}
        assert {r['drawing'].replace('\\','/') for r in proof}==expected and len(expected)==6
        for r in proof:
            assert r['dimensions']==7 and all(r[k] for k in ('regenerate','save_reopen','identities_retained'))
            assert digest(args.references/r['drawing'])==r['source_sha256']
            assert digest(args.references/r['part'])==r['part_sha256']
    if args.kind=='knurled-nuts':
        assert args.gui_verification and args.drawing_verification and args.catalog_verification
        catalog={r['filename']:r for r in json.loads(args.catalog_verification.read_text(encoding='utf-8'))}
        assert set(catalog)=={r['filename'] for r in rows}
        for row in rows:
            case=catalog[row['filename']]
            assert case['source_sha256']==digest(args.models/row['filename']) and case['catalog_edits']>=2
            assert all(case[k] for k in ('independent_volume_and_mass','reference_identity','derived_stock',
                'thread_follows_height','undo_redo','save_reopen'))
        gui=json.loads(args.gui_verification.read_text(encoding='utf-8'))
        assert {r['filename'] for r in gui}=={'KN-CSN021461-M06.0.prtz','KN-CSN021462-M06.0.prtz'}
        for r in gui:
            assert r['source_sha256']==digest(args.models/r['filename']) and r['mouse_dimension_edits']==9
            assert all(r[k] for k in ('undo_redo','save_reopen','regenerate'))
            assert r['independent_geometry'] and digest(Path(r['output']))==r['output_sha256']
        proof=json.loads(args.drawing_verification.read_text(encoding='utf-8'))
        expected={p.relative_to(args.references).as_posix() for p in args.references.rglob('*.drwz')}
        assert {r['drawing'].replace('\\','/') for r in proof}==expected and len(expected)==12
        assert all(r['dimensions']>0 and all(r[k] for k in ('regenerate','save_reopen','identities_retained')) for r in proof)
        for r in proof:
            assert digest(args.references/r['drawing'])==r['source_sha256']
            assert digest(args.references/r['part'])==r['part_sha256']
    previous={r['path']:r['sha256'] for r in json.loads(args.replace_receipt.read_text(encoding='utf-8'))} if args.replace_receipt else {}
    copies=[];changes={};identities=set();families={}
    for v in rows:
        source=args.models/v['filename'];case=cases[source.name];lib=destination(args.kind,v);index=lib/'0000-index'
        assert digest(source)==case['source_sha256'] and case['undo_redo'] and case['save_reopen']
        if args.kind in ('special','caps'):
            assert case['successive_edits']==3 and case['exact_geometry_after_each_edit'] and case['thread_references_retained']
        elif args.kind in ('hex-nuts','small-hex-nuts','coupling-nuts'):
            assert case['successive_edits']==6 and case['catalog_edits']>=2 and case['thread_follows_height']
            assert case['reference_identity'] and case['analytical_volume_after_each_edit'] and case['derived_stock']
        elif args.kind in ('castle-nuts','small-castle-nuts'):
            assert case['successive_edits']==(15 if v['crown_diameter_mm'] and not v.get('crown_diameter_from_flats') else 12) and case['catalog_edits']>=2
            assert all(case[k] for k in ('reference_identity','independent_volume_after_each_edit',
                'trimmed_thread_after_each_edit','thread_follows_height','derived_stock'))
        elif args.kind=='knurled-nuts':
            assert case['successive_edits']==(12 if v['low'] else 15)
            assert all(case[k] for k in ('independent_volume_and_mass','real_knurl_flanks','thread_clearance',
                'origin_seating','locked_native_dragging','original_blank_thread_face_identity'))
        elif args.kind in ('din-weld','iso-weld'):
            assert case['successive_edits']==(18 if args.kind=='din-weld' else 24)
            assert all(case[k] for k in ('independent_volume_and_mass','three_real_projections',
                'reference_identity','derived_stock','opening_follows_support_face',
                'locked_unlocked_profile_dragging'))
            if args.kind=='din-weld':assert case['opening_follows_pilot']
        elif args.kind=='csn-closed-nuts':
            assert case['successive_edits']==15 and v['standard']=='ČSN 02 1431' and v['closed_profile']=='cone_flat'
            assert all(case[k] for k in ('source_face_identity','independent_volume_and_mass','blind_end_wall',
                'cone_and_flat_equations','thread_extent','derived_stock','locked_unlocked_profile_dragging'))
        elif args.kind=='din-cap-nuts':
            assert case['successive_edits']==15 and v['standard']=='DIN 1587'
            assert all(case[k] for k in ('source_face_identity','independent_volume_and_mass','blind_end_wall',
                'sphere_equation','thread_extent','derived_stock','locked_unlocked_profile_dragging'))
            assert v['bore_depth_mm']==min(v['height_mm']-v['minimum_pole_wall_mm'],v['thread_length_mm']+v['pitch_mm'])
            events=[json.loads(line) for line in (args.thumbnails/'evidence'/source.name/'commands.jsonl').read_text(encoding='utf-8').splitlines()]
            opened=next(e for e in events if e['request']['command']=='open')
            assert Path(opened['request']['arguments']['path']).resolve()==source.resolve()
            assert all(e['response']['ok'] for e in events)
        else:
            assert case['successive_edits']==(18 if args.kind=='tab' else 9) and case['reference_identity'] and case['analytical_volume_after_each_edit'] and case['derived_stock']
        doc=read(source);identity=doc['Document']['document_id'];assert identity not in identities;identities.add(identity)
        # A copied model must remain unambiguous throughout the complete library.
        existing=[f for f in BASE.rglob('*.prtz') if f.name.casefold()==source.name.casefold()]
        assert all(f.resolve()==(lib/source.name).resolve() for f in existing),source.name
        values=json.loads(doc['UserParameterValues']['Data']);assert float(values['mass'][''])>0
        assert set(values['name'])==set(LANGUAGES)
        image=args.thumbnails/(source.name+'.png')
        with Image.open(image) as bitmap:assert bitmap.size==(320,240);bitmap.verify()
        copies.extend(((source,lib/source.name),(image,index/'thumbnails'/image.name)))
        update=changes.setdefault(index,{'Directory':{'Parameters':', '.join(FIELDS),'AutoIndex':'true'},'Parameters':{},'Parts':{},'Files':{}})
        for key in FIELDS:
            for lang in LANGUAGES:update['Parameters'][key+'\\Label\\'+lang]=doc['UserParameterLabels'][key+'\\'+lang]
            for lang,value in values[key].items():update['Parts'][source.stem+'\\'+key+('\\'+lang if lang else '')]=value
        update['Files'].update({source.stem+'\\Path':source.name,source.stem+'\\Provider':'zima-cad',source.stem+'\\Thumbnail':'0000-index/thumbnails/'+image.name})
        families[v['standard']]=index
    for row in refs:
        folder=args.references/row.get('folder',row['standard'].replace('ČSN','CSN').replace(' ',''))
        part=folder/row['part'];drawing=folder/row['drawing'];index=families[row['standard']]
        identity=read(part)['Document']['document_id'];assert identity not in identities;identities.add(identity)
        definition=json.loads(read(drawing)['Document']['param.cpp_drawing'])
        assert definition['source_path']==part.name
        assert all(v['source_path']==part.name for s in definition['sheets'] for v in s['views'])
        if args.kind in ('plain','iso-plain','spring','hex-nuts','small-hex-nuts','coupling-nuts','castle-nuts','small-castle-nuts','knurled-nuts','din-cap-nuts','csn-closed-nuts','din-weld','iso-weld','tab'):
            assert len(definition['sheets'])==1
            sheet=definition['sheets'][0];assert sheet['projection_method']=='first_angle'
            assert len(sheet['views'])==2
            parent=next(v for v in sheet['views'] if not v['parent_view_id'])
            child=next(v for v in sheet['views'] if v['parent_view_id']==parent['id'])
            assert child['projection_direction']=='right'
            assert child['scale']==parent['scale'] and child['source_document_id']==parent['source_document_id']
            assert child['y']==parent['y'] and child['x']<parent['x']
            # First-angle right projection: H'=D, V'=V, D'=-H.
            assert child['camera']['horizontal']==parent['camera']['depth']
            assert child['camera']['vertical']==parent['camera']['vertical']
            assert child['camera']['depth']==[-n for n in parent['camera']['horizontal']]
            for view in (parent,child):
                assert any(a['kind']==1 and a['visible'] and not a['unresolved']
                    for a in view['model_annotations']),'Native illustration axis missing'
            if args.kind not in ('hex-nuts','small-hex-nuts','coupling-nuts','castle-nuts','small-castle-nuts','knurled-nuts','din-cap-nuts','csn-closed-nuts','din-weld','iso-weld'):
                for symbol,angle in (('d1',135),('d2',45)):
                    dimension=next(d for d in sheet['dimensions'] if d['style']['text_override']==symbol)
                    assert dimension['view_id']==parent['id']
                    assert len(dimension['segments'])==1
                    # Layout rotation is relative to the source curve basis;
                    # a major arc need not use the full circle's radial zero.
                    # Verify the actual persisted view-plane text direction.
                    label=dimension['segments'][0]['last_presentation']['label']
                    actual=math.degrees(math.atan2(label[1],label[0]))
                    assert abs(actual-angle)<1e-7,'Washer diameter annotations are not mirrored'
        for path in [part,drawing,part.with_suffix('.png'),*(folder/(part.stem+'_'+lang+'.png') for lang in LANGUAGES)]:
            assert path.is_file();copies.append((path,index/path.name))
        if args.kind in ('din-cap-nuts','csn-closed-nuts','coupling-nuts','din-weld','iso-weld'):
            for lang in LANGUAGES:
                path=folder/(part.stem+'_'+lang+'.drwz')
                assert path.is_file();copies.append((path,index/path.name))
    for source,target in copies:
        assert target.resolve().is_relative_to(BASE.resolve())
        if target.exists() and digest(target)!=digest(source):
            assert previous.get(target.relative_to(ROOT).as_posix())==digest(target),'Different existing asset: '+str(target)
    # All geometry/edit receipts, files, native identities and image sizes passed.
    for source,target in copies:
        if not target.exists() or digest(target)!=digest(source):target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
    for index,update in changes.items():
        path=index/'metadata.ini'
        if not path.exists():path.write_text('[Directory]\n',encoding='utf-8')
        installer.update_ini(path,update)
        path.write_text(path.read_text(encoding='utf-8').replace('Parameters="'+', '.join(FIELDS)+'"','Parameters='+', '.join(FIELDS)),encoding='utf-8')
    receipt=[dict(path=p.relative_to(ROOT).as_posix(),sha256=digest(p)) for _,p in copies]
    (args.models/('installed-'+args.kind+'.json')).write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    print(f'Installed {len(rows)} native {args.kind} models, metadata, thumbnails and {len(refs)} localized native drawings')


if __name__=='__main__':main()
