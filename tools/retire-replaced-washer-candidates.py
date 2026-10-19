"""Retire only unchanged generated CSN assets superseded by an ISO install.

Both receipts are required. Unlisted files and user-authored metadata survive.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'config/lib/03-ENGINEERING-TABLES/05-FASTENERS/06-WASHERS'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--previous',type=Path,required=True);p.add_argument('--current',type=Path,required=True)
    p.add_argument('--receipt',type=Path,required=True);args=p.parse_args()
    old=json.loads(args.previous.read_text(encoding='utf-8'));new=json.loads(args.current.read_text(encoding='utf-8'))
    assert len(old)==286 and len(new)==324,'Unexpected washer installation inventory'
    retained={r['path'] for r in new};obsolete=[r for r in old if r['path'] not in retained]
    paths=[];retired_models={}
    for row in obsolete:
        path=(ROOT/row['path']).resolve()
        assert path.is_relative_to(BASE.resolve()) and path.relative_to(BASE).parts[0] in ('01-HEXAGON','02-CAP-HEAD')
        assert path.name.startswith('FW-CSN') and path.suffix in ('.prtz','.drwz','.png')
        assert path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256'],path
        index=BASE/path.relative_to(BASE).parts[0]/'0000-index'
        for html in index.glob('*.html'):
            assert path.name not in html.read_text(encoding='utf-8'),('Asset still referenced by HTML',path,html)
        if path.suffix=='.prtz' and path.parent==index.parent:retired_models.setdefault(index,set()).add(path.stem)
        paths.append(path)
    metadata={}
    for index,stems in retired_models.items():
        path=index/'metadata.ini';section='';lines=[]
        for line in path.read_text(encoding='utf-8').splitlines():
            if line.startswith('[') and line.endswith(']'):section=line[1:-1]
            if section in ('Parts','Files') and '=' in line and line.split('=',1)[0].split('\\',1)[0] in stems:continue
            lines.append(line)
        metadata[path]='\n'.join(lines)+'\n'
    # Every path and digest passed before deleting even the first generated file.
    for path in paths:path.unlink()
    for path,text in metadata.items():path.write_text(text,encoding='utf-8')
    args.receipt.write_text(json.dumps(obsolete,indent=2)+'\n',encoding='utf-8')
    print(f'Retired {len(paths)} exact generated files, including {sum(len(v) for v in retired_models.values())} superseded CSN models')


if __name__=='__main__':main()
