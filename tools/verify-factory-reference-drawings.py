"""Verify resolved factory reference dimensions through regeneration and reopen."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    p.add_argument('--root',type=Path,default=ROOT/'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS')
    p.add_argument('--drawing',type=Path,action='append',help='Explicit native drawings within root, including localized staged illustrations')
    p.add_argument('--output',type=Path,required=True);args=p.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    args.root=args.root.resolve()
    drawings=sorted(path.resolve() for path in args.drawing) if args.drawing else sorted(
        path for topic in ('02-FITTED','03-STUDS','04-CAP-HEAD','06-PAN-HEAD')
        for path in (args.root/topic).rglob('*-REFERENCE.drwz') if path.parent.name=='0000-index')
    assert drawings,'No native reference drawings found'
    assert len(drawings)==len(set(drawings)) and all(path.is_relative_to(args.root) for path in drawings)
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    sources={drawing:sha(drawing) for drawing in drawings};parts={}
    for drawing in drawings:
        document=configparser.RawConfigParser();document.read(drawing,encoding='utf-8')
        part=(drawing.parent/json.loads(document['Document']['param.cpp_drawing'])['source_path']).resolve()
        assert part.is_relative_to(args.root) and part.is_file()
        sources[part]=sha(part);parts[drawing]=part
    results=[]
    for index,source in enumerate(drawings):
        target=output/f'copy-{index}.drwz';assert not target.exists()
        commands=[('open',dict(path=str(source.resolve()))),('regenerate',{}),('drawing.dimension.list',{}),('drawing.annotation.list',dict(kind='dimension',mode='erase')),
            ('save_as',dict(path=str(target))),('close',dict(discard=True)),('open',dict(path=str(target))),
            ('regenerate',{}),('drawing.dimension.list',{}),('drawing.annotation.list',dict(kind='dimension',mode='erase')),('close',dict(discard=True))]
        payload=''.join(json.dumps(dict(command=command,arguments=arguments))+'\n' for command,arguments in commands)
        process=subprocess.run([str(args.cli.resolve()),'--working-directory',str(output),'--stdin'],cwd=output,
            input=payload.encode(),capture_output=True,timeout=240)
        (output/f'{index}.stdout.log').write_bytes(process.stdout);(output/f'{index}.stderr.log').write_bytes(process.stderr)
        assert process.returncode==0,(source,process.returncode,process.stderr[-1000:])
        records=[json.loads(line) for line in process.stdout.splitlines() if line.strip()]
        assert len(records)==len(commands) and all(record['ok'] for record in records),records
        before=records[2]['data']['items'];after=records[8]['data']['items']
        assert before and len(before)==len(after)
        assert all(d['state']=='resolved' for d in before+after),(source,before,after)
        for first,second in zip(before,after):
            assert first['dimension']==second['dimension'] and len(first['measurements'])==len(second['measurements'])
            for a,b in zip(first['measurements'],second['measurements']):assert abs(a['value']-b['value'])<1e-6
        model_before=records[3]['data']['items'];model_after=records[9]['data']['items']
        assert len(model_before)==len(model_after)
        original_part=configparser.RawConfigParser();original_part.read(parts[source],encoding='utf-8')
        copied_drawing=configparser.RawConfigParser();copied_drawing.read(target,encoding='utf-8')
        copied_definition=json.loads(copied_drawing['Document']['param.cpp_drawing'])
        copied_path=(target.parent/copied_definition['source_path']).resolve()
        assert copied_path.is_relative_to(output) and copied_path.is_file()
        copied_part=configparser.RawConfigParser();copied_part.read(copied_path,encoding='utf-8')
        for a,b in zip(model_before,model_after):
            assert all(a['reference'][k]==b['reference'][k] for k in ('owner','key','instance_path')) and a['view']==b['view']
            # Save As intentionally clones the native source Part with a new
            # document identity. References must follow that exact copy while
            # retaining the authored owner and semantic dimension identities.
            assert a['reference']['source_document']==original_part['Document']['document_id']
            assert b['reference']['source_document']==copied_part['Document']['document_id']==copied_definition['source_document_id']
            assert a['visible'] and b['visible'] and not a['unresolved'] and not b['unresolved']
            assert abs(a['value']-b['value'])<1e-8 and a['text']==b['text']
        results.append(dict(drawing=str(source.relative_to(args.root)),source_sha256=sources[source],
            part=str(parts[source].relative_to(args.root)),part_sha256=sources[parts[source]],
            dimensions=len(before),model_dimensions=len(model_before),regenerate=True,save_reopen=True,identities_retained=True))
        print('Verified '+source.name,flush=True)
    assert all(sha(path)==value for path,value in sources.items()),'Verification modified factory source files'
    (output/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(len(results),'read-only native reference Drawings verified')


if __name__=='__main__':main()
