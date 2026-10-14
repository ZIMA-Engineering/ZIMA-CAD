"""Check generated reference PDFs for selectable, unclipped paginated A4 output."""
import argparse
import json
from pathlib import Path

import pdfplumber


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=Path(__file__).parent / 'data/unit-tables.json')
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[1]
    data = json.loads(args.data.read_text(encoding='utf-8'))
    evidence = repository / 'build/reference-table-verification' / args.data.stem
    results, failures = [], []
    for topic in data['pages']:
        if not topic['rows']:
            continue
        for language in data['languages']:
            name = (topic['path'] or 'root').replace('/', '_') + '_' + language + '.pdf'
            path = evidence / name
            if not path.is_file():
                failures.append({'file': name, 'error': 'Missing rendered PDF'})
                continue
            with pdfplumber.open(path) as document:
                for number, page in enumerate(document.pages, 1):
                    if abs(page.width - 595.28) > 1 or abs(page.height - 841.89) > 1:
                        failures.append({'file': name, 'page': number, 'error': 'Expected portrait A4'})
                    if not page.chars:
                        failures.append({'file': name, 'page': number, 'error': 'No selectable text'})
                    clipped = [char for char in page.chars if char['x0'] < 20 or char['x1'] > page.width - 20
                               or char['top'] < 20 or char['bottom'] > page.height - 20]
                    if clipped:
                        failures.append({'file': name, 'page': number, 'error': 'Text exceeds printable bounds', 'characters': len(clipped)})
                results.append({'file': name, 'pages': len(document.pages),
                                'characters': sum(len(p.chars) for p in document.pages)})
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'print.json').write_text(json.dumps({'results': results, 'failures': failures}, indent=2), encoding='utf-8')
    print(json.dumps({'pages': len(results), 'failures': failures}))
    raise SystemExit(bool(failures))


if __name__ == '__main__':
    main()
