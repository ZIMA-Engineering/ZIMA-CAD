"""Independent math, source-status, language and page-splitting checks."""
import html
import importlib.util
import json
import re
import unittest
import math
from decimal import Decimal, localcontext
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('reference_generator', ROOT / 'tools/generate-reference-tables.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def rows_for(data, prefix):
    # Wide tables repeat key columns. Reassemble by stored source row/column
    # identity to test equations and ensure splits introduce no inconsistent copy.
    result = {}
    for page in (block for topic in data['pages'] for block in generator.table_blocks(topic)):
        if not page['rows'] or page.get('source_table') != prefix:
            continue
        for offset, row in enumerate(page['rows']):
            # Separate tables within one original topic retain different prefixes.
            index = (page.get('source_path',page['path']).rsplit('/', 1)[-1].split('-', 1)[0], page['source_rows'][0] + offset)
            assembled = result.setdefault(index, {})
            for column, cell in zip(page['source_columns'], row):
                if column in assembled:
                    assert assembled[column] == cell, (prefix, index, column)
                assembled[column] = cell
    return [[values[i] for i in range(max(values) + 1)] for _, values in sorted(result.items())]


class ReferenceChapters(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tolerance = generator.load_data(ROOT / 'tools/data/tolerance-tables.json')
        cls.thread = generator.load_data(ROOT / 'tools/data/thread-tables.json')

    def test_calculated_metric_profiles(self):
        checked = 0
        paths = {p.get('source_table') for p in self.thread['pages'] if p.get('source_table') and
                 (p['source_table'].startswith('02-METRIC/01-GENERAL/') or p['source_table'] == '02-METRIC/02-PRECISION')}
        for path in paths:
            for row in rows_for(self.thread, path):
                match = re.match(r'M\s*(\d+(?:[,.]\d+)?)', row[0])
                self.assertIsNotNone(match, row)
                diameter = Decimal(match[1].replace(',', '.'))
                pitch = Decimal(row[1])
                with localcontext() as context:
                    context.prec = 50
                    height = Decimal(3).sqrt() * pitch / 2
                    expected = [diameter - 5 * height / 4, diameter - 3 * height / 4, diameter - 17 * height / 12]
                    for actual, value in zip(row[2:], expected):
                        self.assertLessEqual(abs(Decimal(actual) - value), Decimal('.0005'))
                self.assertGreater(diameter, Decimal(row[3]))
                self.assertGreater(Decimal(row[3]), Decimal(row[2]))
                checked += 1
        self.assertEqual(checked, 706)

    def test_engagement_ranges_and_duplicate_source(self):
        first = rows_for(self.thread, '06-TRAPEZOID/03-ENGAGE')
        second = rows_for(self.thread, '06-TRAPEZOID/03-ENGAGE-02')
        self.assertFalse(second)
        self.assertFalse(any(p['path'].startswith('06-TRAPEZOID/03-ENGAGE-02') for p in self.thread['pages']))
        self.assertEqual(len(first), 16)
        for row in first:
            a, b, pitch, lower, upper, long = [Decimal(s.replace(',', '.')) for s in row]
            self.assertLess(a, b)
            self.assertGreater(pitch, 0)
            self.assertLess(lower, upper)
            self.assertEqual(upper, long)

    def test_unverified_sources_never_claim_current_numeric_compliance(self):
        for data in [self.tolerance, self.thread]:
            for page in data['pages']:
                if not page['rows'] or not page.get('source_table'):
                    continue
                calculated = data is self.thread and (
                    page['source_table'].startswith('02-METRIC/01-GENERAL/') or page['source_table'] == '02-METRIC/02-PRECISION')
                glossary = data is self.tolerance and page['source_table'] == '02-ISO-FITS/08-TERMS'
                if not calculated and not glossary:
                    self.assertIn(page.get('review'), ['source_data', 'historical'], page['path'])
        # Persist the contradictory original numbers for later source review;
        # a width equation cannot decide which source field was wrong.
        holes = rows_for(self.tolerance, '02-ISO-FITS/09-HOLES/01-A-D')
        self.assertTrue(any('+850 | +540' in row for row in holes))

    def test_localization_generation_and_assets(self):
        czech_prose = re.compile(r'[ěščřžýáíéúůďťňó]|\b(?:od|do|přes)\b', re.I)
        for data in [self.tolerance, self.thread]:
            root = ROOT / data['root']
            for page in data['pages']:
                for row in page['rows']:
                    for cell in row:
                        if isinstance(cell, str):
                            self.assertIsNone(czech_prose.search(cell), (page['path'], cell))
                for language in generator.LANGUAGES:
                    path = root / page['path'] / '0000-index' / f'index_{language}.html'
                    if not page['rows']:
                        self.assertFalse(path.exists(), path)
                        continue
                    self.assertEqual(path.read_text(encoding='utf-8'), generator.render(data, page, language))
                    document = html.unescape(path.read_text(encoding='utf-8'))
                    self.assertIn(generator.localized(data, 'reference_notice', language), document)
                    if page.get('review'):
                        self.assertIn(generator.localized(data, page['review'], language), document)
                    for target in re.findall(r'(?:src|href)="([^"]+)"', document):
                        if not target.startswith(('https:', 'http:')):
                            self.assertTrue((path.parent / target).is_file(), (path, target))

    def test_original_whitworth_profile_and_scan_removal(self):
        profile_spec = importlib.util.spec_from_file_location('whitworth_profile', ROOT / 'tools/generate-whitworth-profile.py')
        profile = importlib.util.module_from_spec(profile_spec)
        profile_spec.loader.exec_module(profile)
        h_triangle, h_profile, radius = profile.geometry(1)
        self.assertAlmostEqual(h_profile, .6403273756570553, places=12)
        self.assertAlmostEqual(radius, .1373290834621564, places=9)
        theta = math.radians(27.5)
        # The vertical rounding cutback is H/6, independently checking tangency.
        self.assertAlmostEqual(radius * (1 / math.sin(theta) - 1), h_triangle / 6, places=12)
        self.assertEqual(profile.TARGET.read_text(encoding='utf-8'), profile.drawing())
        for name in ['01.png', '02.png', 'nakres.png']:
            self.assertFalse((profile.TARGET.parent / name).exists())
        for relative in ['05-PIPE/0000-index/01.png', '05-PIPE/0000-index/nakres.png',
                         '05-PIPE/01-ISO-7/0000-index/01.png', '05-PIPE/02-ISO-228/0000-index/01.png',
                         '07-BICYCLES/0000-index/nakres.png']:
            self.assertFalse((ROOT / self.thread['root'] / relative).exists())

    def test_metric_and_trapezoid_schematic_angles(self):
        diagram_spec = importlib.util.spec_from_file_location('basic_profiles', ROOT / 'tools/generate-basic-thread-profiles.py')
        diagram = importlib.util.module_from_spec(diagram_spec)
        diagram_spec.loader.exec_module(diagram)
        for angle, folder, name in [(60, '02-METRIC', 'metric-profile.svg'), (30, '06-TRAPEZOID', 'trapezoidal-profile.svg')]:
            svg = diagram.profile(angle)
            self.assertEqual((diagram.ROOT / folder / '0000-index' / name).read_text(encoding='utf-8'), svg)
            points = [[float(s) for s in pair.split(',')] for pair in re.search(r'<polyline points="([^"]+)"', svg)[1].split()]
            dx, dy = points[2][0] - points[1][0], points[2][1] - points[1][1]
            self.assertAlmostEqual(2 * math.degrees(math.atan2(dx, dy)), angle, places=5)
            self.assertAlmostEqual(points[4][0] - points[0][0], 260, places=5)
        for angle in [55,60]:
            svg=diagram.angle_profile(angle)
            self.assertEqual((diagram.ROOT/'0000-index/diagrams'/f'thread-angle-{angle}.svg').read_text(encoding='utf-8'),svg)
            points=[[float(s) for s in pair.split(',')] for pair in re.search(r'<polyline points="([^"]+)"',svg)[1].split()]
            self.assertAlmostEqual(points[2][0]-points[0][0],240,places=5)
            self.assertAlmostEqual(2*math.degrees(math.atan((points[1][0]-points[0][0])/(points[1][1]-points[0][1]))),angle,places=5)


if __name__ == '__main__':
    unittest.main()
