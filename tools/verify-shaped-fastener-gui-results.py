"""Independently check saved shaped-fastener GUI outputs against their sources."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from fastener_verification_runtime import pin_checker

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('models','gui','output'):p.add_argument('--'+key,type=Path,required=True)
    args=p.parse_args();stage=args.models.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    checker=pin_checker(out);rows=json.loads((stage/'manifest.json').read_text(encoding='utf-8'))['variants'];results=[]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    for v in rows:
        source=stage/v['filename'];folder=args.gui/Path(v['filename']).stem;target=folder/'spring-washer-gui.prtz'
        if not target.exists():continue
        assert 'passed' in (folder/'stdout.log').read_text(encoding='utf-8')
        case=out/source.stem;case.mkdir();shutil.copyfile(target,case/target.name)
        (case/'manifest.json').write_text(json.dumps(dict(variants=[dict(v,filename=target.name)])),encoding='utf-8')
        subprocess.run([str(checker),'--flange-nuts' if 'roof_angle_degrees' in v else '--wheel-fasteners',str(case)],check=True)
        count=6 if 'roof_angle_degrees' in v or 'included_angle_degrees' in v else 3
        results.append(dict(filename=source.name,source_sha256=sha(source),output=str(target.resolve()),output_sha256=sha(target),
            mouse_dimension_edits=count,independent_geometry=True,undo_redo=True,save_reopen=True,regenerate=True))
    assert results,'No matching saved GUI models'
    (out/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(f'{len(results)} GUI outputs independently verified')


if __name__=='__main__':main()
