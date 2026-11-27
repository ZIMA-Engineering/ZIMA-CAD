"""Install hash-verified native key candidates and their calculated previews."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LIBRARY=ROOT/'config/lib/03-ENGINEERING-TABLES'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--verification',type=Path,required=True)
    parser.add_argument('--thumbnails',type=Path,required=True)
    args=parser.parse_args()
    rows=json.loads((args.stage/'manifest.json').read_text(encoding='utf-8'))
    verified={row['filename']:row for row in json.loads(args.verification.read_text(encoding='utf-8'))}
    existing={p.name.casefold():p for p in LIBRARY.rglob('*.prtz')}
    pending=[]
    for row in rows:
        source=args.stage/row['filename'];receipt=verified[source.name]
        assert receipt['readback'] and receipt['repeated_edits']==4
        assert hashlib.sha256(source.read_bytes()).hexdigest()==receipt['source_sha256']
        folder=LIBRARY/'08-KEYS-KEYWAYS'/('04-WOODRUFF' if row['woodruff'] else '02-PARALLEL')
        target=folder/source.name
        assert source.name.casefold() not in existing,'Duplicate native library filename: '+source.name
        image=args.thumbnails/(source.stem+'.png');assert image.is_file()
        pending.extend([(source,target),(image,folder/'0000-index'/'thumbnails'/image.name)])
        existing[source.name.casefold()]=target
    for source,target in pending:
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
        assert hashlib.sha256(source.read_bytes()).digest()==hashlib.sha256(target.read_bytes()).digest()
    print(f'Installed {len(rows)} verified native keys with previews')

if __name__=='__main__':main()
