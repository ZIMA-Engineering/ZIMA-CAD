"""Check the authored general-reference data against independent mathematical equations."""
import json
import html
import math
import re
import runpy
import unittest
from decimal import Decimal
from fractions import Fraction
from pathlib import Path

GENERATOR=runpy.run_path(str(Path(__file__).with_name('generate-reference-tables.py')))
DATA=GENERATOR['load_data'](Path(__file__).with_name('data')/'general-tables.json')


def angle(text):
    degrees,minutes,seconds=map(float,re.findall(r'[\d.]+',text))
    return degrees+minutes/60+seconds/3600


class GeneralReferences(unittest.TestCase):
    def test_all_generated_languages_links_and_metadata(self):
        root=GENERATOR['REPOSITORY']/DATA['root']
        for page in DATA['pages']:
            folder=root/page['path']/'0000-index'
            self.assertEqual((folder/'metadata.ini').read_text(encoding='utf-8'),GENERATOR['metadata'](DATA,page))
            for language in DATA['languages']:
                path=folder/f'index_{language}.html'
                if not page['rows']:
                    self.assertFalse(path.exists());continue
                text=path.read_text(encoding='utf-8')
                self.assertEqual(text,GENERATOR['render'](DATA,page,language))
                self.assertIn(DATA['labels']['reference_notice'][DATA['languages'].index(language)],html.unescape(text))
                for ref in re.findall(r'href="([^"]+)"',text):
                    if not ref.startswith('https://'):
                        self.assertTrue((folder/ref).resolve().is_file(),ref)

    def test_every_integer_and_fractional_inch_conversion(self):
        integers=fractions=0
        for page in DATA['pages']:
            if '/01-INTEGER-' in page['path']:
                for row in page['rows']:
                    for offset,value in enumerate(row[1:]):
                        self.assertEqual(Decimal(value),Decimal(int(row[0])+offset)*Decimal('25.4'))
                        integers+=1
            if '/02-FRACTIONS-' in page['path']:
                for row in page['rows']:
                    for offset in [0,3]:
                        value=Fraction(row[offset]);decimal=Decimal(value.numerator)/Decimal(value.denominator)
                        self.assertEqual(Decimal(row[offset+1]),decimal)
                        self.assertEqual(Decimal(row[offset+2]),decimal*Decimal('25.4'))
                        fractions+=1
        self.assertEqual((integers,fractions),(330,64))

    def test_slope_and_conical_angle_equations(self):
        count=0
        for page in DATA['pages']:
            if page['title'].startswith('slope_angles'):
                for beta,slope,wedge in page['rows']:
                    beta=math.radians(angle(beta))
                    self.assertAlmostEqual(float(wedge[2:]),1/(2*math.tan(beta/2)),places=6)
                    if slope!='—':self.assertAlmostEqual(float(slope[2:]),1/math.tan(beta),places=6)
                    count+=1
            if page['title'].startswith('taper_ratios'):
                for taper,full,half in page['rows']:
                    n=float(taper[2:]);actual=math.degrees(math.atan(1/(2*n)))
                    self.assertLessEqual(abs(angle(full)-2*actual),.051/3600)
                    self.assertLessEqual(abs(angle(half)-actual),.051/3600)
                    count+=1
        self.assertEqual(count,34)

    def test_preferred_numbers_and_preserved_size_order(self):
        for page in DATA['pages']:
            if page['path'].startswith(('02-NUMBERS/','03-SIZES/')) and page['headers'][:2]==['order','value']:
                values={}
                for row in page['rows']:
                    for i in range(0,len(row),2):
                        if row[i]!='—' and re.fullmatch(r'[\d.]+',row[i+1]):values[int(row[i])]=Decimal(row[i+1])
                ordered=[values[i] for i in sorted(values)]
                self.assertTrue(all(a<b for a,b in zip(ordered,ordered[1:])),page['path'])
                if 'THEORETICAL-R40' in page['path']:
                    for index,value in values.items():self.assertAlmostEqual(float(value),10**((index-1)/40),places=6)


if __name__=='__main__':unittest.main()
