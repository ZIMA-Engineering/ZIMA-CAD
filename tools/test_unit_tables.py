"""Independent equations and factory-link/localization checks for unit reference assets."""
import html
import json
import re
import runpy
import unittest
from decimal import Decimal
from pathlib import Path

GENERATOR = runpy.run_path(str(Path(__file__).with_name('generate-reference-tables.py')))
DATA = GENERATOR['load_data']()
ROOT = GENERATOR['ROOT']


def dimension(expression):
    # Exponents in (length, mass, time, electric current, temperature, amount, light).
    base = {
        'm': (1,0,0,0,0,0,0), 'kg': (0,1,0,0,0,0,0), 's': (0,0,1,0,0,0,0),
        'A': (0,0,0,1,0,0,0), 'K': (0,0,0,0,1,0,0), 'mol': (0,0,0,0,0,1,0),
        'cd': (0,0,0,0,0,0,1), 'rad': (0,0,0,0,0,0,0), 'sr': (0,0,0,0,0,0,0),
        'Hz': (0,0,-1,0,0,0,0), 'N': (1,1,-2,0,0,0,0), 'Pa': (-1,1,-2,0,0,0,0),
        'J': (2,1,-2,0,0,0,0), 'W': (2,1,-3,0,0,0,0), 'C': (0,0,1,1,0,0,0),
        'V': (2,1,-3,-1,0,0,0), 'F': (-2,-1,4,2,0,0,0), 'Ω': (2,1,-3,-2,0,0,0),
        'S': (-2,-1,3,2,0,0,0), 'Wb': (2,1,-2,-1,0,0,0), 'T': (0,1,-2,-1,0,0,0),
        'H': (2,1,-2,-2,0,0,0), '°C': (0,0,0,0,1,0,0), 'lm': (0,0,0,0,0,0,1),
        'lx': (-2,0,0,0,0,0,1), 'Bq': (0,0,-1,0,0,0,0), 'Gy': (2,0,-2,0,0,0,0),
        'Sv': (2,0,-2,0,0,0,0), 'kat': (0,0,-1,0,0,1,0), '1': (0,0,0,0,0,0,0)}
    result = [0] * 7
    numerator, *denominator = expression.split('/')
    for part, sign in [(numerator, 1), *((value, -1) for value in denominator)]:
        for factor in part.strip('()').split('·'):
            unit, _, exponent = factor.partition('^')
            power = int(exponent) if exponent else 1
            for i, value in enumerate(base[unit]):
                result[i] += sign * power * value
    return tuple(result)


class UnitReferenceTest(unittest.TestCase):
    def test_generated_assets_and_local_links_in_all_languages(self):
        identifiers = set()
        for page in DATA['pages']:
            folder = ROOT / page['path'] / '0000-index'
            metadata = (folder / 'metadata.ini').read_text(encoding='utf-8')
            self.assertEqual(metadata, GENERATOR['metadata'](DATA, page))
            identifier = re.search(r'^Id=(.*)$', metadata, re.M)[1]
            self.assertNotIn(identifier, identifiers)
            identifiers.add(identifier)
            for lang in DATA['languages']:
                if not page['rows']:
                    self.assertFalse((folder / f'index_{lang}.html').exists())
                    continue
                document = (folder / f'index_{lang}.html').read_text(encoding='utf-8')
                self.assertEqual(document, GENERATOR['render'](DATA, page, lang))
                self.assertIn(f'<html lang="{lang}">', document)
                for ref in re.findall(r'href="([^"]+)"', document):
                    if not ref.startswith('https://'):
                        self.assertTrue((folder / html.unescape(ref)).resolve().is_file(), ref)
                self.assertNotIn('<script', document)
        self.assertEqual(len(identifiers), 26)

    def test_all_derived_unit_equations_independently(self):
        checked = 0
        special = set()
        for page in DATA['pages']:
            if not page['path'].startswith('02-DERIVED-UNITS/'):
                continue
            for row in page['rows']:
                if page['headers'] == ['quantity', 'base_expression', 'symbol']:
                    expressed, symbol = row[1], row[2]
                else:
                    symbol, expressed = row[2], row[3]
                    if isinstance(row[1], dict):
                        special.add(symbol)
                self.assertEqual(dimension(symbol), dimension(expressed), row)
                checked += 1
        self.assertEqual(checked, 49)
        self.assertEqual(len(special), 22)

    def test_exact_imperial_conversions_from_defining_values(self):
        inch = Decimal('0.0254')
        foot = 12 * inch
        yard = 3 * foot
        mile = 5280 * foot
        gallon = 231 * inch ** 3 * 1000  # litres
        bushel = Decimal('2150.42') * inch ** 3 * 1000
        pound = Decimal('0.45359237')
        expectations = {
            'square_foot': foot ** 2, 'square_yard': yard ** 2, 'square_mile': mile ** 2 / 10**6,
            'acre': 4840 * yard ** 2, 'cubic_inch': inch ** 3 * 10**6,
            'cubic_foot': foot ** 3 * 1000, 'cubic_yard': yard ** 3,
            'gallon_us': gallon, 'gallon_uk': Decimal('4.54609'), 'pint_us': gallon / 8,
            'pint_uk': Decimal('4.54609') / 8, 'pint_dry': bushel / 64,
            'ounce_us': gallon / 128 * 1000, 'ounce_uk': Decimal('4.54609') / 160 * 1000,
            'bushel_us': bushel, 'bushel_uk': Decimal('4.54609') * 8,
            'barrel_oil': gallon * 42, 'barrel_dry': 7056 * inch ** 3 * 1000,
            'quart_us': gallon / 4, 'quart_uk': Decimal('4.54609') / 4,
            'quart_dry': bushel / 32, 'cup': gallon / 16 * 1000, 'gill': gallon / 32 * 1000,
            'peck': bushel / 4, 'cord': foot ** 3 * 128, 'water_ton': Decimal('4.54609') * 224,
            'square_build': 100 * foot ** 2, 'square_rod': (Decimal('16.5') * foot) ** 2,
            'long_ton': 2240 * pound, 'short_ton': 2000 * pound,
            'ounce_mass': pound / 16 * 1000, 'hundredweight': 112 * pound,
            'ounce_troy': pound / 7000 * 480 * 1000, 'pound_troy': pound / 7000 * 5760 * 1000,
            'scruple': pound / 7000 * 20 * 1000, 'pennyweight': pound / 7000 * 24 * 1000,
        }
        checked = set()
        for page in DATA['pages']:
            if not page['path'].startswith(('06-', '07-')):
                continue
            for row in page['rows']:
                key = row[0]['key']
                if key in expectations:
                    last = row[-1].split('=')[-1].strip()
                    number = re.match(r'[\d. ]+', last)[0].strip().replace(' ', '')
                    self.assertEqual(Decimal(number), expectations[key], row)
                    checked.add(key)
        self.assertEqual(checked, set(expectations))

    def test_si_prefixes_and_corrected_critical_values(self):
        page = next(p for p in DATA['pages'] if p['title'] == 'prefixes')
        self.assertEqual(len(page['rows']), 12)
        self.assertEqual(page['rows'][0][:2], ['10^30', 'Q'])
        self.assertEqual(page['rows'][0][3:5], ['10^-30', 'q'])
        self.assertEqual(page['rows'][1][:2], ['10^27', 'R'])
        common = next(p for p in DATA['pages'] if p['title'] == 'common')
        litre = next(r for r in common['rows'] if r[1]['key'] == 'litre')
        self.assertEqual(litre[-1], '1 L = 1 dm^3 = 10^-3 m^3')
        mechanics = next(p for p in DATA['pages'] if p['path'] == '02-DERIVED-UNITS/01-MECHANICS')
        self.assertNotEqual(dimension('Pa·s'), dimension('m^2/s'))
        self.assertEqual(Decimal('299792458') * Decimal('365.25') * Decimal('86400'), Decimal('9460730472580800'))


if __name__ == '__main__':
    unittest.main()
