"""Independent geometry, source preservation and localization checks."""
import importlib.util
import json
import math
import re
import unittest
from decimal import Decimal, localcontext
from pathlib import Path
from lxml import etree

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('reference_generator', ROOT / 'tools/generate-reference-tables.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def number(text):
    return float(text.strip('()').replace(',', '.'))


class ShaftTables(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = generator.load_data(ROOT / 'tools/data/shaft-tables.json')
        cls.tables = cls.data['curated_source_tables']
        cls.original = json.loads((ROOT / 'tools/data/shaft-retained-source-tables.json').read_text(encoding='utf-8'))

    def test_every_split_preserves_all_source_cells(self):
        reconstructed = {}
        for page in (block for topic in self.data['pages'] for block in generator.table_blocks(topic)):
            if not page.get('source_table'):
                continue
            table = reconstructed.setdefault(page['source_table'], {})
            for offset, row in enumerate(page['rows']):
                cells = table.setdefault(page['source_rows'][0] + offset, {})
                for column, cell in zip(page['source_columns'], row):
                    if column in cells:
                        self.assertEqual(cells[column], cell)
                    cells[column] = cell
        self.assertEqual(set(reconstructed), set(self.tables))
        for name, source in self.tables.items():
            self.assertEqual(len(reconstructed[name]), len(source['rows']), name)
            for i, row in enumerate(source['rows']):
                self.assertEqual(set(reconstructed[name][i]), set(range(len(row))), (name, i))
                self.assertEqual([reconstructed[name][i][j] for j in range(len(row))],
                                 [cell or '—' for cell in row], (name, i))

    def test_retained_normative_data_against_original_source(self):
        # Independent source evidence, not the curated table compared to itself.
        # These originals are tracked historical inputs, not downloaded standards.
        paths = ['01-SHAFT-END/01-SIZES', '03-FINE/01-STRAIGHT',
                 '03-FINE/02-INVOLUTE', '04-STRAIGHT/01-LIGHT',
                 '04-STRAIGHT/02-MEDIUM', '04-STRAIGHT/03-HEAVY',
                 '05-INVOLUTE', '06-GEARS/12-PROFILE',
                 '06-GEARS/13-PITCH', '06-GEARS/14-FLANKS']
        checked = 0
        for topic in paths:
            source = self.original['tables'][topic]
            self.assertEqual(source['sha256'], self.data['original_source_sha256'][topic + '/0000-index/index_cs.html'])
            rows = source['rows']
            expected = self.tables[topic + '#01']['rows']
            if topic.startswith('04-STRAIGHT/'):
                rows = [[c.replace(' | ', ' … ') for c in row] for row in rows]
            self.assertEqual(rows, expected, topic)
            checked += len(rows)
        self.assertGreater(checked, 300)

    def test_diametral_pitch_exact_reciprocal(self):
        rows = self.tables['06-GEARS/03-MODULE-DP#01']['rows']
        self.assertEqual(len(rows), 43)
        for _, dp, metric in rows:
            with localcontext() as context:
                context.prec = 40
                calculated = Decimal('25.4') / Decimal(dp.replace(',', '.'))
                self.assertLessEqual(abs(Decimal(metric) - calculated), Decimal('.000005'))

    def test_constant_chord_geometric_projection(self):
        rows = self.tables['06-GEARS/07-CHORDAL#01']['rows']
        self.assertEqual(len(rows), 27)
        alpha = math.radians(20)
        for module, chord, height in rows:
            m, actual_chord, actual_height = map(number, [module, chord, height])
            # Rack half-thickness projects onto the contact normal, then onto
            # the chord. Height is the remaining addendum at that contact.
            rack_half = math.pi * m / 4
            normal = rack_half * math.cos(alpha)
            self.assertAlmostEqual(actual_chord / 2, normal * math.cos(alpha), delta=.00005)
            self.assertAlmostEqual(m - actual_height, normal * math.sin(alpha), delta=.00005)
        record = next(c for c in self.data['calculation_corrections'] if c.get('m') == '20')
        self.assertEqual(record['old'], '4,0515')
        self.assertEqual(record['new'], '14.9516')

    def test_span_by_base_pitch_and_tooth_thickness(self):
        rows = self.tables['06-GEARS/09-SPAN#01']['rows']
        self.assertEqual([int(r[0]) for r in rows], list(range(10, 169)))
        alpha = math.radians(20)
        base_pitch = math.pi * math.cos(alpha)
        previous = None
        for z, count, ratio in rows:
            z, k, ratio = int(z), int(count), number(ratio)
            self.assertEqual(k, math.floor((z + 2) / 9) + 1)
            # Retained span counts place the contact on the nominal involute
            # between root and tip circles (root fillets still need clearance).
            contact_parameter = (math.pi * (k - .5) + z * (math.tan(alpha) - alpha)) / z
            contact_radius = z * math.cos(alpha) / 2 * math.sqrt(1 + contact_parameter ** 2)
            self.assertGreater(contact_radius, max(z * math.cos(alpha) / 2, (z - 2.5) / 2))
            self.assertLess(contact_radius, (z + 2) / 2)
            # Evaluate thickness at the base circle plus k-1 base pitches.
            base_thickness = math.cos(alpha) * (math.pi / 2 + z * (math.tan(alpha) - alpha))
            self.assertAlmostEqual(ratio, base_thickness + (k - 1) * base_pitch, delta=.00005)
            if previous and previous[0] == k:
                self.assertAlmostEqual(ratio - previous[1], math.sin(alpha) - alpha * math.cos(alpha), delta=.0001)
            previous = (k, ratio)

    def test_torsion_section_modulus_and_units(self):
        checked = 0
        for name, table in self.tables.items():
            if not name.startswith('01-SHAFT-END/03-TORQUE#'):
                continue
            stresses = [number(re.search(r'τ=(\d+) MPa', self.data['labels'][key][1])[1]) for key in table['headers'][1:]]
            for row in table['rows']:
                d = number(row[0])
                # J=pi*d^4/32 [mm^4], stress=T*r/J; convert N mm to N m.
                polar_moment = math.pi * d ** 4 / 32
                for stress, torque in zip(stresses, row[1:]):
                    self.assertAlmostEqual(number(torque), stress * polar_moment / (d / 2) / 1000, delta=.0005)
                    checked += 1
        self.assertEqual(checked, 225)

    def test_unverified_numbers_stay_marked(self):
        calculated = ['01-SHAFT-END/03-TORQUE#', '06-GEARS/03-MODULE-DP#',
                      '06-GEARS/07-CHORDAL#', '06-GEARS/09-SPAN#']
        for page in self.data['pages']:
            if not page.get('source_table') or any(page['source_table'].startswith(p) for p in calculated):
                continue
            self.assertIn(page.get('review'), ['historical', 'source_data'], page['path'])
        rows = self.tables['05-INVOLUTE/03-RUNOUT#03']['rows']
        self.assertIn('455', rows[2])
        self.assertEqual(self.tables['03-FINE/03-ACCURACY#01']['rows'][0][0], '7,5')
        self.assertEqual(self.tables['03-FINE/03-ACCURACY#02']['rows'][3][-1], '+1,350')
        self.assertIn('corrected 2025-12', self.data['sources']['iso41561']['label'])

    def test_nominal_interval_inclusivity(self):
        for page in self.data['pages']:
            if not page.get('source_table','').startswith(('05-INVOLUTE/02-DEVIATION', '05-INVOLUTE/03-RUNOUT')):
                continue
            for key in page['headers'][2 if '/02-DEVIATION' in page['source_table'] else 1:]:
                header = self.data['labels'][key][1]
                self.assertRegex(header, r'^(?:d ≤12|d >400|\d+ < d ≤\d+) mm$')
        for page in self.data['pages']:
            if page.get('source_table','').startswith(('06-GEARS/12-PROFILE', '06-GEARS/13-PITCH', '06-GEARS/14-FLANKS')):
                bounds = page['rows'][0][0] + '–' + page['rows'][-1][1] + ' mm'
                for title in self.data['labels'][page.get('heading',page['title'])]:
                    self.assertEqual(title.split(' · ')[1], bounds)

    def test_language_files_metadata_and_assets(self):
        czech_prose = re.compile(r'[ěčřžýáíéúůďťňó]|\b(?:od|do|přes|až|nebo)\b', re.I)
        for page in self.data['pages']:
            folder = ROOT / self.data['root'] / page['path'] / '0000-index'
            for row in page['rows']:
                for cell in row:
                    if isinstance(cell, str):
                        self.assertIsNone(czech_prose.search(cell.replace('ČSN', 'CSN')), (page['path'], cell))
            for language in self.data['languages']:
                if page['rows']:
                    self.assertEqual((folder / f'index_{language}.html').read_text(encoding='utf-8'), generator.render(self.data, page, language))
                else:
                    self.assertFalse((folder / f'index_{language}.html').exists())
            self.assertEqual((folder / 'metadata.ini').read_text(encoding='utf-8'), generator.metadata(self.data, page))

    def test_original_diagram_involute_geometry(self):
        source = ROOT / self.data['root'] / '06-GEARS/0000-index/involute-gear.svg'
        document = etree.parse(str(source))
        group = document.xpath('//*[@id="involute-gear"]')[0]
        rp, rb, alpha = float(group.get('data-pitch-radius')), float(group.get('data-base-radius')), math.radians(float(group.get('data-alpha')))
        self.assertAlmostEqual(rb / rp, math.cos(alpha), places=11)
        self.assertEqual(float(group.get('data-module')) * int(group.get('data-z')), 2 * rp)
        outline = group.xpath('./*[local-name()="polygon"]')[0]
        points = [tuple(map(float, pair.split(','))) for pair in outline.get('points').split()]
        # The first tooth flank reaches the pitch circle at the half-thickness
        # angle pi/(2z), independently of its sampled involute parameter.
        polar = [(math.hypot(x - 175, y - 125), math.atan2(y - 125, x - 175)) for x, y in points[:42]]
        before, after = next((p, q) for p, q in zip(polar, polar[1:]) if p[0] <= rp <= q[0])
        theta = before[1] + (after[1] - before[1]) * (rp - before[0]) / (after[0] - before[0])
        self.assertAlmostEqual(abs(theta), math.pi / 40, delta=.00003)


class MergedShaftReferences(unittest.TestCase):
    def test_continuous_tables_align_every_original_cell(self):
        from lxml import html
        for path in ['01-SHAFT-END/03-TORQUE','03-FINE/03-ACCURACY']:
            page=next(p for p in generator.load_data(ROOT/'tools/data/shaft-tables.json')['pages'] if p['path']==path)
            data=generator.load_data(ROOT/'tools/data/shaft-tables.json')
            blocks=list(generator.table_blocks(page))
            headers=page.get('continuous_headers') or list(dict.fromkeys(h for b in blocks for h in b['headers']))
            for language in data['languages']:
                doc=html.fromstring(generator.render(data,page,language))
                tables=doc.xpath('//div[contains(@class,"table-scroll--frozen")]/table')
                self.assertEqual(len(tables),1)
                rows=tables[0].xpath('./tbody/tr');self.assertEqual(len(rows),sum(len(b['rows']) for b in blocks))
                index=0
                for block in blocks:
                    for source_row in block['rows']:
                        values=[''.join(c.itertext()) for c in rows[index]]
                        for key,value in zip(block['headers'],source_row):self.assertEqual(values[headers.index(key)].replace(',','.'),value.replace(',','.'))
                        for key in set(headers)-set(block['headers']):self.assertEqual(values[headers.index(key)],'—')
                        index+=1

if __name__ == '__main__':
    unittest.main()
