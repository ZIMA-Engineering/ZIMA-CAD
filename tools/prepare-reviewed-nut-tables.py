"""Apply independently reviewed nut cells without modifying archived sources."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    path=ROOT/'tools/data/fastener-tables.json'
    data=json.loads(path.read_text(encoding='utf-8'))
    reviewed=json.loads((ROOT/'tools/data/nut-reviewed-dimensions.json').read_text(encoding='utf-8'))
    table=data['curated_source_tables'][reviewed['source_table']]
    blocks=[b for p in data['pages'] for b in [p,*p.get('sections',[])]]
    for c in reviewed['corrections']:
        row,col=c['row'],c['column']
        assert table['headers'][col]=='symbol_'+c['thread']
        assert table['rows'][row][col] in (c['before'],c['after'])
        table['rows'][row][col]=c['after']
        for b in blocks:
            if b.get('source_table')!=reviewed['source_table']:continue
            local=row-b['source_rows'][0]
            if 0<=local<len(b['rows']) and col in b['source_columns']:
                b['rows'][local][b['source_columns'].index(col)]=c['after']
        correction={k:c[k] for k in ('row','column','before','after')}
        correction.update(source_table=reviewed['source_table'],source=reviewed['source'])
        if correction not in data['reviewed_numeric_corrections']:data['reviewed_numeric_corrections'].append(correction)
    data['sources']['iso4034_2012_reviewed']=dict(label='ISO 4034:2012',url=reviewed['source'])
    page=next(p for p in data['pages'] if p['path']=='05-NUTS/01-HEXAGON/08-GRADE-C')
    if 'iso4034_2012_reviewed' not in page['sources']:page['sources'].append('iso4034_2012_reviewed')
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'Applied {len(reviewed["corrections"])} reviewed nut-table corrections')


if __name__=='__main__':main()
