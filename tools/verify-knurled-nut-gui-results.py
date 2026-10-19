"""Independently verify the final native outputs of both knurled-nut GUI tests."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from fastener_verification_runtime import pin_checker

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models',type=Path,required=True)
    parser.add_argument('--gui-results',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    rows=json.loads(args.gui_results.read_text(encoding='utf-8'))
    assert {r['filename'] for r in rows}=={'KN-CSN021461-M06.0.prtz','KN-CSN021462-M06.0.prtz'}
    variants={v['filename']:v for v in json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']}
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    checker=pin_checker(args.output)
    for row in rows:
        name=row['filename'];source=Path(row['output'])
        assert row['source_sha256']==digest(args.models/name)
        folder=args.output.resolve()/Path(name).stem;folder.mkdir(parents=True,exist_ok=True)
        target=folder/name;assert not target.exists()
        expected=digest(source);shutil.copyfile(source,target)
        (folder/'manifest.json').write_text(json.dumps(dict(variants=[variants[name]]),indent=2),encoding='utf-8')
        result=subprocess.run([str(checker),'--knurled-nuts',str(folder)],capture_output=True,timeout=240)
        (folder/'stdout.log').write_bytes(result.stdout);(folder/'stderr.log').write_bytes(result.stderr)
        assert result.returncode==0,(name,result.stderr[-2000:])
        assert digest(source)==expected and digest(target)==expected
        row.update(independent_geometry=True,output_sha256=expected)
        print('Verified final GUI geometry '+name,flush=True)
    args.gui_results.write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
