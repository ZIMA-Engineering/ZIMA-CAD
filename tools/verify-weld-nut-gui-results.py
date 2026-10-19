"""Independently verify geometry saved by the weld-projection mouse-edit checks."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from fastener_verification_runtime import pin_checker


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models',type=Path,required=True)
    parser.add_argument('--filename',required=True)
    parser.add_argument('--gui',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    variant=next(v for v in json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants'] if v['filename']==args.filename)
    execution=json.loads((args.gui/'execution.json').read_text(encoding='utf-8'))
    assert execution['returncode']==0 and execution['source_sha256']==digest(args.models/args.filename)
    source=args.gui/'spring-washer-gui.prtz';before=digest(source)
    target=out/args.filename;assert not target.exists();shutil.copyfile(source,target)
    (out/'manifest.json').write_text(json.dumps(dict(variants=[variant]),indent=2),encoding='utf-8')
    checker=pin_checker(out)
    result=subprocess.run([str(checker),'--weld-nuts',str(out)],capture_output=True,timeout=240)
    (out/'stdout.log').write_bytes(result.stdout);(out/'stderr.log').write_bytes(result.stderr)
    assert result.returncode==0,result.stderr[-2000:]
    assert digest(source)==before and digest(target)==before
    receipt=dict(filename=args.filename,source_sha256=execution['source_sha256'],output_sha256=before,
        mouse_dimension_edits=3 if variant['kind']=='din' else 6,independent_geometry=True,
        retained_external_supports=True,undo_redo=True,save_reopen=True,regenerate=True)
    (out/'results.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print('Weld-nut GUI output independently verified: '+args.filename)


if __name__=='__main__':main()
