"""Fastener data, source preservation, locale and generated-file contracts."""
import importlib.util
import json
import math
import re
import unittest
from collections import Counter
from decimal import Decimal, localcontext
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('reference_generator', ROOT / 'tools/generate-reference-tables.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def numeric_cell(value):
    if not isinstance(value, str):
        return None
    value = value.replace('&pm;', '±').replace('/th>', '').replace(' | ', ' / ')
    value = re.sub(r' \[(?:B|F)\]', '', value)
    value = re.sub(r'(?<=\d) až (?=\d)', '–', value)
    value = re.sub(r'(?<=\d) a (?=\d)', ' / ', value)
    if re.search(r'[A-Za-zА-Яа-яČč]', value) or not re.search(r'\d', value):
        return None
    return re.sub(r'\s+', '', value).replace(',', '.').replace('|', '/')


class Fasteners(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = generator.load_data(ROOT / 'tools/data/fastener-tables.json')
        cls.original = json.loads((ROOT / 'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))

    def test_all_split_cells_reassemble_without_loss(self):
        restored = {}
        for page in (block for topic in self.data['pages'] for block in generator.table_blocks(topic)):
            if not page.get('source_table'):
                continue
            target = restored.setdefault(page['source_table'], {})
            for i, row in enumerate(page['rows']):
                original_row = target.setdefault(page['source_rows'][0] + i, {})
                for col, value in zip(page['source_columns'], row):
                    if col in original_row:
                        self.assertEqual(original_row[col], value)
                    original_row[col] = value
        self.assertEqual(set(restored), set(self.data['curated_source_tables']))
        for name, table in self.data['curated_source_tables'].items():
            self.assertEqual(len(restored[name]), len(table['rows']), name)
            for index, row in enumerate(table['rows']):
                self.assertEqual(set(restored[name][index]), set(range(len(row))), (name, index))
                self.assertEqual([restored[name][index][c] for c in range(len(row))], [v or '—' for v in row], (name, index))

    def test_original_numeric_body_values_retained(self):
        checked = 0
        for name, table in self.data['curated_source_tables'].items():
            topic, number = name.rsplit('#', 1)
            if topic == '01-CAPACITY' and number == '9':
                continue  # Independently calculated area, tested below.
            original = self.original[topic]['tables']
            if not original:
                continue  # Manually transcribed image-only supplier table.
            original = original[int(number)]
            # Header rows differ between original schemas. Body row order and
            # count remain stable; repeated hierarchical headings are collapsed.
            body = original[len(original) - len(table['rows']):]
            before = Counter(numeric_cell(c) for row in body for c in row if numeric_cell(c) is not None)
            for correction in self.data.get('reviewed_numeric_corrections', []):
                if correction['source_table'] != name:
                    continue
                row,column=correction['row'],correction['column']
                self.assertEqual(numeric_cell(body[row][column]),numeric_cell(correction['before']))
                self.assertEqual(table['rows'][row][column],correction['after'])
                self.assertIn(correction['source'], {
                    'https://www.fabory.com/nl_BE/zeskanttapbout-iso-4017-din-933-staal-elektrolytisch-verzinkt-8-8-m4x20/p/01210040020',
                    'https://nahledy.normy.biz/n.php?i=220',
                    'https://nahledy.normy.biz/n.php?i=223',
                })
                before[numeric_cell(correction['before'])]-=1
                before[numeric_cell(correction['after'])]+=1
            after = Counter(numeric_cell(c) for row in table['rows'] for c in row if numeric_cell(c) is not None)
            self.assertEqual(before, after, name)
            checked += sum(before.values())
        self.assertGreater(checked, 15000)

    def test_nominal_tensile_area_independently(self):
        rows = self.data['curated_source_tables']['01-CAPACITY#9']['rows']
        self.assertEqual(len(rows), 19)
        m4 = next(r for r in rows if r[0] == 'M4')
        self.assertEqual(m4[1], '0.7')
        self.assertAlmostEqual(float(m4[2]), 8.779, delta=.001)
        for thread, pitch, actual in rows:
            d = Decimal(re.match(r'M([0-9,]+)', thread)[1].replace(',', '.'))
            p = Decimal(pitch.replace(',', '.'))
            # The traditional approximate area diameter is the mean of the
            # pitch and external root diameters. Check it independently from
            # the rounded coefficient used to generate the reference table.
            with localcontext() as context:
                context.prec = 45
                h = Decimal(3).sqrt() * p / 2
                pitch_diameter = d - 3 * h / 4
                external_root = d - 17 * h / 12
                exact_basic = Decimal(str(math.pi)) * ((pitch_diameter + external_root) / 2) ** 2 / 4
                calculated = Decimal(str(math.pi)) * (d - Decimal('.9382') * p) ** 2 / 4
                self.assertLessEqual(abs(Decimal(actual) - calculated), Decimal('.00005'))
                self.assertLess(abs(calculated - exact_basic) / exact_basic, Decimal('.00001'))

    def test_product_grade_and_property_class_are_distinct(self):
        self.assertEqual(self.data['sources']['iso4014_2022']['label'], 'ISO 4014:2022')
        self.assertIn('Product grade', self.data['labels']['product_grade'][1])
        self.assertIn('Property class', self.data['labels']['property_class'][1])
        grades = next(p for p in self.data['pages'] if p['path'] == '03-MECHANIC')
        requirements = generator.load_data(ROOT / 'tools/data/remaining-tables.json')
        context = next(p for p in requirements['pages'] if p['path'] == '06-FASTENER-REQUIREMENTS')
        for language in generator.LANGUAGES:
            # Unlike measured decimals, standardized class identifiers retain
            # their full stop in every display language.
            self.assertIn('4.6, 5.6, 8.8, 10.9, 12.9', generator.render(self.data, grades, language))
            self.assertIn('8.8 ≠ A/B/C', generator.render(requirements, context, language))
            self.assertIn('A2 ≠ 8.8', generator.render(requirements, context, language))
            self.assertNotIn('8,8', generator.render(requirements, context, language))
        for family in ['01-GRADE-AB', '03-FULL-AB', '05-FINE-AB', '06-FULL-FINE']:
            context = next(block for p in self.data['pages'] for block in generator.table_blocks(p) if block.get('source_path') == '04-SCREWS/01-HEX-HEAD/' + family + '/00-CONTEXT')
            self.assertIn([{'key': 'product_grade'}, 'A/B'], context['rows'])
        for page in self.data['pages']:
            if page['path'].startswith('04-SCREWS/04-CAP-HEAD/01-SOCKET'):
                for title in self.data['labels'][page['title']]:
                    self.assertNotRegex(title, r'grade C|Produktklasse C|класс C')
            if page['path'].startswith('04-SCREWS/01-HEX-HEAD/06-FULL-FINE'):
                for title in self.data['labels'][page['title']]:
                    self.assertNotIn('ISO 8765', title)
        body = str(self.data['curated_source_tables'])
        self.assertIn('[B]', body)
        self.assertIn('[F]', body)
        self.assertIn('→ ISO 4017', body)

    def test_current_references_do_not_certify_old_numeric_values(self):
        for page in self.data['pages']:
            if page.get('source_table') and page['source_table'] != '01-CAPACITY#9':
                self.assertIn(page.get('review'), ['historical', 'source_data'], page['path'])
        self.assertIn('iso4026_2026', self.data['sources'])
        self.assertIn('iso10642_2026', self.data['sources'])
        self.assertIn('Amd 1:2026', self.data['sources']['iso4042_2022']['label'])
        self.assertGreater(len(self.data['replaced_requirement_tables']), 20)
        for item in self.data['replaced_requirement_tables']:
            self.assertTrue(self.original[item['path']]['tables'][item['table']])

    def test_five_language_outputs_and_no_stale_landing_pages(self):
        prose = re.compile(r'[ěčřžýáíéúůďťňó]|\b(?:dle|podle|přes|až|nebo)\b', re.I)
        root = ROOT / self.data['root']
        for page in self.data['pages']:
            folder = root / page['path'] / '0000-index'
            self.assertEqual((folder / 'metadata.ini').read_text(encoding='utf-8'), generator.metadata(self.data, page))
            for row in page['rows']:
                for cell in row:
                    if isinstance(cell, str):
                        self.assertIsNone(prose.search(cell.replace('ČSN', 'CSN')), (page['path'], cell))
            for language in self.data['languages']:
                path = folder / f'index_{language}.html'
                if page['rows']:
                    self.assertEqual(path.read_text(encoding='utf-8'), generator.render(self.data, page, language))
                else:
                    self.assertFalse(path.exists())
            self.assertFalse((folder / 'index.html').exists())

    def test_source_identities_and_legacy_path_safety(self):
        for page in self.data['pages']:
            self.assertNotIn('..', page['path'])
        for original in self.data['legacy_index_files']:
            self.assertEqual(original['sha256'], self.data['original_source_sha256'][original['path']])
        for topic, source in self.original.items():
            path = (topic + '/' if topic != '.' else '') + '0000-index/' + source['file']
            self.assertEqual(source['sha256'], self.data['original_source_sha256'][path])


class MergedFastenerReferences(unittest.TestCase):
    def test_shared_branding_can_be_linked_unlinked_or_omitted(self):
        from lxml import html
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        page=next(p for p in data['pages'] if p['path']=='01-CAPACITY')
        for language in data['languages']:
            document=html.fromstring(generator.render(data,page,language))
            self.assertEqual(document.xpath('//header/a[@class="reference-brand"]/@href'),['https://zima-engineering.cz/'])
            self.assertEqual(document.xpath('//header/nav/a/@lang'),data['languages'])
        unlinked={**data,'branding':{**data['branding'],'url':''}}
        self.assertEqual(len(html.fromstring(generator.render(unlinked,page,'en')).xpath('//header/span[@class="reference-brand"]/img')),1)
        omitted={**data,'branding':None}
        self.assertFalse(html.fromstring(generator.render(omitted,page,'en')).xpath('//header//img'))

    def test_thread_rows_preserve_every_dimension_and_length_table(self):
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        pages=[page for page in data['pages'] if page.get('thread_dimension_rows')]
        self.assertEqual(len(pages),6)
        for page in pages:
            blocks=generator.screen_table_blocks(page)
            combined=blocks[1]
            self.assertEqual(combined['headers'][0],'symbol_d')
            self.assertEqual(sum(len(block['rows']) for block in blocks),page['display_row_count'])
            for source in page['sections']:
                if source['headers'][0]!='parameter':
                    self.assertIn(source,blocks)
                    continue
                positions=[];occurrences={}
                for row in source['rows']:
                    name=row[0] if isinstance(row[0],dict) else {'literal':row[0]}
                    candidates=[i for i,header in enumerate(combined['headers']) if header==name]
                    token=json.dumps(name,sort_keys=True)
                    occurrence=occurrences.get(token,0);occurrences[token]=occurrence+1
                    positions.append(candidates[occurrence])
                for column,thread in enumerate(source['headers'][1:],1):
                    expected=[row[column] for row in source['rows']]
                    self.assertTrue(any(row[0]=={'key':thread} and [row[i] for i in positions]==expected for row in combined['rows']), (page['path'],thread))

    def test_original_cells_survive_join_and_transpose(self):
        from lxml import html
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        for path in ['01-CAPACITY','02-HOLES']:
            page=next(p for p in data['pages'] if p['path']==path)
            for language in data['languages']:
                index=data['languages'].index(language)
                doc=html.fromstring(generator.render(data,page,language))
                tables=doc.xpath('//div[contains(@class,"table-scroll--frozen")]/table')
                self.assertEqual(len(tables),1)
                rows=[[c.text_content() for c in row] for row in tables[0].xpath('./tbody/tr')]
                headers=[c.text_content() for c in tables[0].xpath('./thead/tr/th')]
                self.assertEqual(len(rows),page['display_row_count'])
                def text(value):
                    if isinstance(value,dict):return data['labels'][value['key']][index]
                    return html.fromstring('<span>'+generator.mathematical(value,language)+'</span>').text_content()
                def equal(a,b):
                    if a==b:return True
                    if numeric_cell(a) is None or numeric_cell(b) is None:return False
                    try:return Decimal(numeric_cell(a))==Decimal(numeric_cell(b))
                    except ArithmeticError:return numeric_cell(a)==numeric_cell(b)
                for block in generator.table_blocks(page):
                    if path=='02-HOLES':
                        for column,key in enumerate(block['headers'][1:],1):
                            actual=next(r for r in rows if r[0]==data['labels'][key][index])
                            for source in block['rows']:
                                self.assertTrue(equal(actual[headers.index(text(source[0]))],text(source[column])))
                    else:
                        columns=[headers.index(data['labels'][key][index]) for key in block['headers']]
                        for source in block['rows']:
                            self.assertTrue(any(all(equal(row[col],text(value)) for col,value in zip(columns,source)) for row in rows),(language,source))
    def test_original_counterbore_drawing_is_embedded_locally(self):
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        page=next(p for p in data['pages'] if p['path']=='02-HOLES')
        self.assertEqual(page['figures'][0]['path'],'01.png')
        self.assertTrue((ROOT/data['root']/page['path']/'0000-index/01.png').is_file())
        import base64
        import xml.etree.ElementTree as ET
        figures=ROOT/'config/lib/03-ENGINEERING-TABLES/0000-index/diagrams'
        image=ET.parse(figures/'hex-screw-reference.svg').getroot().find('.//{http://www.w3.org/2000/svg}image')
        self.assertTrue(image.attrib['href'].startswith('data:image/png;base64,'))
        self.assertEqual(base64.b64decode(image.attrib['href'].split(',',1)[1]),(figures/'hex-screw-native.png').read_bytes())


if __name__ == '__main__':
    unittest.main()
