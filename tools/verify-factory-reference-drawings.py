"""Verify resolved factory reference dimensions through regeneration and reopen."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe')
    p.add_argument('--root',type=Path,default=ROOT/'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/04-SCREWS')
    p.add_argument('--output',type=Path,required=True);args=p.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    drawings=sorted(path for topic in ('02-FITTED','03-STUDS','04-CAP-HEAD')
        for path in (args.root/topic).rglob('*-REFERENCE.drwz') if path.parent.name=='0000-index')
    assert drawings,'No native reference drawings found'
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    sources={path:sha(path) for drawing in drawings for path in (drawing,drawing.with_suffix('.prtz'))}
    results=[]
    for index,source in enumerate(drawings):
        target=output/f'copy-{index}.drwz';assert not target.exists()
        commands=[('open',dict(path=str(source.resolve()))),('regenerate',{}),('drawing.dimension.list',{}),
            ('save_as',dict(path=str(target))),('close',dict(discard=True)),('open',dict(path=str(target))),
            ('regenerate',{}),('drawing.dimension.list',{}),('close',dict(discard=True))]
        payload=''.join(json.dumps(dict(command=command,arguments=arguments))+'\n' for command,arguments in commands)
        process=subprocess.run([str(args.cli.resolve()),'--working-directory',str(output),'--stdin'],cwd=output,
            input=payload.encode(),capture_output=True,timeout=240)
        (output/f'{index}.stdout.log').write_bytes(process.stdout);(output/f'{index}.stderr.log').write_bytes(process.stderr)
        assert process.returncode==0,(source,process.returncode,process.stderr[-1000:])
        records=[json.loads(line) for line in process.stdout.splitlines() if line.strip()]
        assert len(records)==len(commands) and all(record['ok'] for record in records),records
        before=records[2]['data']['items'];after=records[7]['data']['items']
        assert before and len(before)==len(after)
        assert all(d['state']=='resolved' for d in before+after),(source,before,after)
        for first,second in zip(before,after):
            assert first['dimension']==second['dimension'] and len(first['measurements'])==len(second['measurements'])
            for a,b in zip(first['measurements'],second['measurements']):assert abs(a['value']-b['value'])<1e-6
        results.append(dict(drawing=str(source.relative_to(args.root)),dimensions=len(before),regenerate=True,save_reopen=True,identities_retained=True))
        print('Verified '+source.name,flush=True)
    assert all(sha(path)==value for path,value in sources.items()),'Verification modified factory source files'
    (output/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(len(results),'read-only native reference Drawings verified')


if __name__=='__main__':main()
