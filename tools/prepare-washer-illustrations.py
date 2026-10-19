"""Attach checked native washer art while retaining historical table data."""
import argparse
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--spring-art',type=Path,required=True);p.add_argument('--plain-art',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    groups={}
    for kind,root in [('spring',args.spring_art),('plain',args.plain_art)]:
        for r in json.loads((root/'illustrations.json').read_text(encoding='utf-8')):
            topic='03-SPRING' if kind=='spring' else '02-CAP-HEAD' if r['standard'] in ('ČSN 02 1703','ISO 7092') else '01-HEXAGON'
            stem=Path(r['drawing']).stem;key='native_washer_'+stem.replace('-','_')
            data['labels'][key]=[r['standard']+(' '+r['form'] if r['form'] else '')+' — '+t for t in
                ('výkres nativního modelu','native model drawing','Zeichnung des nativen Modells','dessin du modèle natif','чертёж нативной модели')]
            groups.setdefault('06-WASHERS/'+topic,[]).append(dict(path=stem+'.png',caption=key,
                localized_paths={lang:stem+'_'+lang+'.png' for lang in data['languages']}))
    for page in data['pages']:
        if page['path'] in groups:
            page.update(figures=groups[page['path']],localized_illustrations=True,figures_side_by_side=True)
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
