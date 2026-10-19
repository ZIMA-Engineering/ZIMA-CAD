"""Apply the shared row-per-thread presentation to every nut dimension table."""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tables',ROOT/'tools/generate-reference-tables.py')
tables=importlib.util.module_from_spec(spec);spec.loader.exec_module(tables)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    data=json.loads((ROOT/'tools/data/fastener-tables.json').read_text(encoding='utf-8'))
    for page in data['pages']:
        if not page['path'].startswith('05-NUTS/'):continue
        dimensions=[b for b in tables.presentation_table_blocks(page) if b['headers'][0]=='parameter']
        if not dimensions:continue
        page.update(thread_dimension_rows=True,freeze_header=True)
        page['display_row_count']=sum(len(b['rows']) for b in tables.screen_table_blocks(page))
    args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
