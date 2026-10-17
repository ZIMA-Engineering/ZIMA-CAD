"""Stage renamed factory fasteners and remove their redundant grade parameter.

Native parameters are edited by the shared C++ command host. Original factory
files and user documents remain untouched until separate installation checks.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fastener_author',ROOT/'tools/generate-fastener-library.py')
author=importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)


def native_cache(path):
    # This is read-only verification, never a native serialization writer.
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.startswith('data=') and '"format":"zima-shared-cache-v1"' in line:
            return hashlib.sha256(line.encode()).hexdigest()
    raise RuntimeError('Calculated native geometry is missing: '+str(path))


def identity(v):
    return tuple(v[k] for k in ('family','standard','diameter_mm','pitch_mm','product_grade','length_mm'))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--old-manifest',type=Path,required=True)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    parser.add_argument('--filename')
    args=parser.parse_args()
    old=json.loads(args.old_manifest.read_text(encoding='utf-8'))['variants']
    current=json.loads(args.manifest.read_text(encoding='utf-8'))['variants']
    lookup={identity(v):v for v in old}
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    records=[]
    for variant in current:
        if args.filename and variant['filename']!=args.filename:continue
        previous=lookup[identity(variant)]
        source=author.LIBRARY/previous['family']/previous['filename']
        target=output/variant['filename']
        if target.exists():raise RuntimeError('Use a fresh candidate directory: '+str(target))
        digest=hashlib.sha256(source.read_bytes()).hexdigest();cache=native_cache(source)
        shutil.copyfile(source,target)
        native=author.Native(args.cli.resolve(),output/'evidence'/target.stem)
        try:
            opened=native.run('open',path=str(target));document=next(v['id'] for v in opened if v['active'])
            parameters=native.run('document.parameters.get')['parameters']
            expected=[v for v in parameters if v['key']!='product_grade']
            native.run('document.parameters.set',parameters=expected)
            native.run('save');native.run('close')
            reopened=native.run('open',path=str(target))
            assert next(v['id'] for v in reopened if v['active'])==document
            assert native.run('document.parameters.get')['parameters']==expected
            assert not native.run('history.list')['calculation_errors']
            native.run('close')
        finally:native.close()
        assert native_cache(target)==cache,'Metadata-only editing changed calculated geometry'
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest,'Original factory source changed'
        records.append(dict(filename=target.name,previous_filename=source.name,family=variant['family'],
            source_sha256=digest,sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
            unchanged_calculated_cache=cache,document=document))
        (output/'consolidation.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
        print('Consolidated '+target.name,flush=True)
    if not records:parser.error('No variants selected')
    print(f'{len(records)} candidates retain exact calculated geometry and document identity')


if __name__=='__main__':main()
