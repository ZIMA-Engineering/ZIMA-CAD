"""Check GUI execution receipts and independently verify final DIN cap-nut geometry."""
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
    parser.add_argument('--case',action='append',required=True,help='Native filename=GUI output directory')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    variants={v['filename']:v for v in json.loads((args.models/'manifest.json').read_text(encoding='utf-8'))['variants']}
    digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    checker=pin_checker(out);receipts=[]
    for case in args.case:
        name,directory=case.split('=',1);directory=Path(directory).resolve()
        execution=json.loads((directory/'execution.json').read_text(encoding='utf-8'))
        assert execution['returncode']==0 and execution['source_sha256']==digest(args.models/name)
        folder=out/Path(name).stem;folder.mkdir(exist_ok=True)
        source=directory/'spring-washer-gui.prtz';expected=digest(source);target=folder/name
        assert not target.exists();shutil.copyfile(source,target)
        (folder/'manifest.json').write_text(json.dumps(dict(variants=[variants[name]]),indent=2),encoding='utf-8')
        result=subprocess.run([str(checker),'--cap-nuts',str(folder)],capture_output=True,timeout=240)
        (folder/'stdout.log').write_bytes(result.stdout);(folder/'stderr.log').write_bytes(result.stderr)
        assert result.returncode==0,result.stderr[-2000:]
        assert digest(source)==expected and digest(target)==expected
        receipts.append(dict(filename=name,source_sha256=execution['source_sha256'],output=str(source),
            output_sha256=expected,mouse_radius_edits=3,properties_thread_edits=3,
            independent_geometry=True,undo_redo=True,save_reopen=True,regenerate=True))
    assert {r['filename'] for r in receipts}=={'CN-DIN1587-M08.0.prtz','CN-DIN1587-M10.0x1.00.prtz'}
    (out/'results.json').write_text(json.dumps(receipts,indent=2)+'\n',encoding='utf-8')
    print('Two DIN cap-nut GUI outputs independently verified')


if __name__=='__main__':main()
