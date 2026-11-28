"""Reproduce reviewed groove dimensions from local supplier PDFs (requires pypdf).

The PDFs stay outside Git. Only the reviewed numerical tables are extracted.
Changed supplier files require another review; their hashes are not accepted
automatically. See doc/MECH_GROOVE.md for scope and source links.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    '471': ('datenblatt_seeger-ringe_471.pdf', '57a47b2f6a0f4cdca92afd332b3aa9ad5d4f08f6cfbaaabea2fd67b62732aaaf'),
    '472': ('datenblatt_seeger-ringe_472.pdf', 'c4dc6913f7a92bfb4ec38a8d7ba5ac4b4b62b2c295bb04814eb6fc40d3c2fbf2'),
    '9925': ('datenblatt_seeger-ringe_Sprengringe_9925_9926_Wellen.pdf', '5773ec37f132f52de90304f628736496e25cf90da24dd81c0925384d82b4ec63'),
    '9926': ('datenblatt_seeger-ringe_Sprengringe_9925_9926_Bohrungen.pdf', '798407dd5a0fc4f99f48d3206e6299da457934c9ff46eb29ebb04fd8fb9e5664'),
}
NUMBER = r'\d+(?:,\d+)?'


def number(value):
    return float(value.replace(',', '.'))


def catalogue(source_dir):
    from pypdf import PdfReader
    pages = {}
    for key, (_, digest) in SOURCES.items():
        path = source_dir / (key + '.pdf')
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'Unreviewed supplier PDF: {key}')
        pages[key] = [page.extract_text(extraction_mode='layout') for page in PdfReader(path).pages]
    result = []
    for standard, label, internal, expected in [('471', 'A', False, 200), ('472', 'J', True, 200)]:
        rows = []
        for index in range(0, 8, 2):
            nominal = re.findall(r'\b' + label + r'\s+(\d+)\**[ \t]+\1[ \t]+', pages[standard][index])
            grooves = re.findall(r'^\s*(' + NUMBER + r')\s+[+–−-]\s*(' + NUMBER + r')\s+(' + NUMBER + r')\s+(' + NUMBER + r')\s+', pages[standard][index+1], re.M)
            assert len(nominal) == len(grooves), (standard, index)
            for d, (d2, tolerance, width, depth) in zip(nominal, grooves):
                d, d2, width, depth = map(number, (d, d2, width, depth))
                assert abs(abs(d-d2)/2-depth) < 1e-7 and (d2>d) == internal
                # Reviewed ISO 286 IT13 steps, applied to nominal groove width.
                assert width <= 10
                h13 = .14 if width <= 3 else .18 if width <= 6 else .22
                rows.append(dict(id=f'{label}{d:g}', diameter=d, groove_diameter=d2,
                    width_min=width, width_max=round(width+h13, 9), radius=0))
        assert len(rows) == expected
        result.append(dict(id='din'+standard+'-seeger', standard='DIN '+standard,
            source='https://www.seeger-orbis.com/fileadmin/assets/pdf_datenblaetter/'+SOURCES[standard][0],
            internal=internal, profile='rectangular', rows=rows))
    for standard, label, internal, expected in [('9925', 'RW', False, 40), ('9926', 'RB', True, 37)]:
        rows = []
        for line in '\n'.join(pages[standard]).splitlines():
            match = re.search(r'\b'+label+r'\s+(\d+)[ \t]+\1[ \t]+', line)
            if not match:
                continue
            values = [number(x) for x in re.findall(NUMBER, line[match.start():])]
            # Designation, nominal, wire, free diameter, tolerance, gap, mass,
            # groove diameter, groove tolerance, cutter radius, optional rpm.
            assert len(values) in (10, 11), (standard, line)
            d, d2, radius = values[1], values[7], values[9]
            assert d == values[0]
            depth = abs(d-d2)/2
            assert (d2>d) == internal and 0<depth<2*radius
            width = 2*(depth*(2*radius-depth))**.5
            rows.append(dict(id=f'{label}{d:g}', diameter=d, groove_diameter=d2,
                width_min=width, width_max=width, radius=radius))
        assert len(rows) == expected
        result.append(dict(id='din'+standard+'-seeger', standard='DIN '+standard,
            source='https://www.seeger-orbis.com/fileadmin/assets/pdf_datenblaetter/'+SOURCES[standard][0],
            internal=internal, profile='circular', rows=rows))
    return dict(schema=1, catalogues=result,
        review='Supplier geometry, reviewed 2026-11-28; not a certification of the current DIN editions. Rectangular width uses nominal minimum plus selectable H13 tolerance. Circular opening width is derived.',
        width_tolerance_source='https://www.schaeffler.com/remotemedien/media/_shared_media/08_media_library/01_publications/schaeffler_2/catalogue_1/downloads_6/stt_de_de.pdf')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, required=True, help='Directory with 471.pdf, 472.pdf, 9925.pdf and 9926.pdf')
    parser.add_argument('--output', type=Path, default=ROOT/'resources/data/mech/grooves.json')
    parser.add_argument('--check', action='store_true', help='Compare without changing the output file')
    args = parser.parse_args()
    data = catalogue(args.source_dir)
    if args.check:
        assert json.loads(args.output.read_text(encoding='utf-8')) == data, 'Groove catalogue differs from reviewed sources'
    else:
        args.output.write_text(json.dumps(data, indent=2)+'\n', encoding='utf-8')
    print('Verified 477 groove sizes from four reviewed supplier PDFs.')
