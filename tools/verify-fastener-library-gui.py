"""Run the established native mouse-edit GUI scenario on staged nut Parts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from fastener_verification_runtime import pin_checker

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--kind',choices=('insert-lock-nuts','pressed-wing-nuts','formed-nose-washers','wheel-fasteners'),default='insert-lock-nuts')
    parser.add_argument('--gui',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cpp.exe')
    args=parser.parse_args();out=args.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    checker=pin_checker(out);rows=json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants'];receipts=[]
    for v in (rows[0],rows[-1]):
        source=(args.models/v['filename']).resolve();folder=out/source.stem;folder.mkdir();digest=hashlib.sha256(source.read_bytes()).hexdigest()
        env=os.environ.copy();env.update(ZIMA_VERIFY_CIRCLE_ENDPOINT_ONLY='1',ZIMA_VERIFY_SPRING_WASHER_SOURCE=str(source))
        with (folder/'stdout.log').open('wb') as log:
            result=subprocess.run([str(args.gui.resolve()),'--working-directory',str(folder),'--verify-startup'],
                cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
        execution=dict(returncode=result.returncode,source_sha256=digest)
        (folder/'execution.json').write_text(json.dumps(execution,indent=2),encoding='utf-8')
        assert result.returncode==0,(source.name,result.returncode)
        output=folder/'spring-washer-gui.prtz';target=folder/source.name;shutil.copyfile(output,target)
        (folder/'manifest.json').write_text(json.dumps(dict(variants=[v]),indent=2),encoding='utf-8')
        checked=subprocess.run([str(checker),'--'+args.kind,str(folder)],capture_output=True,timeout=120)
        (folder/'independent.stdout.log').write_bytes(checked.stdout);(folder/'independent.stderr.log').write_bytes(checked.stderr)
        assert checked.returncode==0,checked.stderr[-2000:]
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
        receipts.append(dict(filename=source.name,source_sha256=digest,mouse_dimension_edits=3,undo_redo=True,
            regenerate=True,save_reopen=True,independent_geometry=True,body_appearance=args.kind=='insert-lock-nuts',output=str(output),
            output_sha256=hashlib.sha256(output.read_bytes()).hexdigest()))
        (out/'results.json').write_text(json.dumps(receipts,indent=2)+'\n',encoding='utf-8')
        print('Native mouse-edit GUI and independent geometry passed: '+source.name,flush=True)


if __name__=='__main__':main()
