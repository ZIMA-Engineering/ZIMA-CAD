"""Check the authored general-reference data against independent mathematical equations."""
import json
import hashlib
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
    def test_consolidated_normal_dimensions_preserve_authored_values(self):
        page=next(p for p in DATA['pages'] if p['path']=='03-SIZES')
        self.assertFalse(any(p['path'].startswith('03-SIZES/') for p in DATA['pages']))
        self.assertTrue(page['freeze_header'])
        originals=[]
        for column,count in enumerate([36,71,141,281],1):
            values=[(int(row[0]),row[column]) for row in page['rows'] if row[column]!='—']
            self.assertEqual(len(values),count)
            originals.append(values)
        for section in page['sections'][:2]:
            originals.append([(int(row[0]),row[1]) for row in section['rows']])
        for column,count in enumerate([8,11,24],1):
            values=[(int(row[0]),row[column]) for row in page['sections'][2]['rows'] if row[column]!='—']
            self.assertEqual(len(values),count)
            originals.append(values)
        self.assertEqual(sum(map(len,originals)),759)
        digest=hashlib.sha256(json.dumps(originals,ensure_ascii=False,separators=(',',':')).encode()).hexdigest()
        self.assertEqual(digest,'a684f6a2715c03f60bbcb335fa390805d40e863a57b362b2c8aa4b23c07dfd60')
        title=next(p['title'] for p in DATA['pages'] if p['path']=='')
        self.assertTrue(all(value==value.upper() for value in DATA['labels'][title]))

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
        preferred=next(p for p in DATA['pages'] if p['path']=='02-NUMBERS')
        self.assertFalse(any(p['path'].startswith('02-NUMBERS/') for p in DATA['pages']))
        self.assertTrue(preferred['freeze_header'])
        self.assertEqual(len(preferred['rows']),161)
        for column,count in enumerate([6,11,21,41,81,161],1):
            values=[Decimal(row[column]) for row in preferred['rows'] if row[column]!='—']
            self.assertEqual(len(values),count)
            self.assertEqual((values[0],values[-1]),(Decimal(1),Decimal(10)))
            self.assertTrue(all(a<b for a,b in zip(values,values[1:])))
        theory=preferred['sections'][0]
        self.assertEqual(len(theory['rows']),41)
        for index,value in theory['rows']:
            self.assertAlmostEqual(float(value),10**((int(index)-1)/40),places=6)
        for language in DATA['languages']:
            rendered=GENERATOR['render'](DATA,preferred,language)
            self.assertIn('table-scroll--frozen',rendered)
            self.assertIn('tabindex="0" role="region"',rendered)
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
