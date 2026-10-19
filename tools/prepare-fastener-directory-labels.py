"""Keep screw directory labels descriptive while retaining standards inside pages."""
import argparse
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    standard=re.compile(r'\s*[·—]\s*(?:ČSN|DIN|ISO)\s+\d[\d .:/-]*(?=\s*[·—]|$)')
    count=0
    for page in data['pages']:
        if page['path']!='04-SCREWS' and not page['path'].startswith('04-SCREWS/'):continue
        old=data['labels'][page['title']]
        new=[standard.sub('',text).strip() for text in old]
        assert all(new)
        if new!=old:count+=1;data['labels'][page['title']]=new
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'{count} screw directory labels simplified in five languages')


if __name__=='__main__':main()
