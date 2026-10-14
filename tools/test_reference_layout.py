"""Whole-library source preservation, localization and logical-layout checks."""
import importlib.util,json,math,re,unittest
from collections import Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('references',ROOT/'tools/generate-reference-tables.py')
generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)


class LogicalReferences(unittest.TestCase):
    def test_remaining_original_data_is_not_silently_repaired(self):
        data=generator.load_data(ROOT/'tools/data/remaining-tables.json')
        original=json.loads((ROOT/'tools/data/remaining-source-tables.json').read_text(encoding='utf-8'))
        rebuilt={}
        for topic in data['pages']:
            for block in generator.table_blocks(topic):
                if not block.get('source_table'):continue
                rows=rebuilt.setdefault(block['source_table'],{})
                for i,row in enumerate(block['rows']):
                    target=rows.setdefault(block['source_rows'][0]+i,{})
                    for c,value in zip(block['source_columns'],row):
                        if c in target:self.assertEqual(target[c],value)
                        target[c]=value
        self.assertEqual(set(rebuilt),set(data['curated_source_tables']))
        numeric=lambda s: re.sub(r'\s+','',s.replace('&plus;','+').replace('&pm;','±').replace(' až ','…').replace(',','.'))
        checked=0
        for name,table in data['curated_source_tables'].items():
            for i,row in enumerate(table['rows']):
                self.assertEqual([rebuilt[name][i][c] for c in range(len(row))],row)
            topic,index=name.rsplit('#',1)
            old=original[topic]['tables'][int(index)][table['original_header_rows']:]
            # Compare numeric values independently of the curated translations.
            old_values=Counter(numeric(c) for row in old for c in row if re.search(r'\d',c) and not re.search(r'[A-Za-zěščřžýáíéúů]',c.replace('&plus;','').replace('&pm;','').replace('až','')))
            new_values=Counter(numeric(c) for row in table['rows'] for c in row if isinstance(c,str) and re.search(r'\d',c) and not re.search(r'[A-Za-zěščřžýáíéúů]',c))
            self.assertEqual(old_values,new_values,name);checked+=sum(old_values.values())
        self.assertGreater(checked,3000)

    def test_logical_topics_and_section_localization(self):
        pages=0;sections=0
        for name in ['unit','general','tolerance','thread','shaft','fastener','remaining']:
            data=generator.load_data(ROOT/f'tools/data/{name}-tables.json')
            for topic in data['pages']:
                if not topic['rows']:continue
                pages+=1
                for block in generator.table_blocks(topic):
                    sections+=1
                    for row in block['rows']:
                        for cell in row:
                            if isinstance(cell,str):
                                self.assertIsNone(re.search(r'[ěčřůďťň]|\b(?:přes|podle|dle|až)\b',cell.replace('ČSN','CSN'),re.I),(name,topic['path'],cell))
                    for key in block['headers']:
                        self.assertEqual(len(data['labels'][key]),5)
                for lang in data['languages']:
                    text=generator.render(data,topic,lang)
                    self.assertEqual(text.count('<tbody>'),len(list(generator.table_blocks(topic))))
                    self.assertIn(f'<html lang="{lang}">',text)
                    for figure in topic.get('figures',[]):
                        self.assertLess(text.index('<figure'),text.index('<table>'))
        self.assertEqual(pages,459)
        self.assertGreater(sections,pages)

    def test_analytical_examples_and_unit_factors(self):
        # Independent examples check dimensional conversions used by new pages.
        density=7850;area=math.pi*20**2/4;length=1000
        mass=density*area*length*1e-9
        self.assertAlmostEqual(mass,2.46615023306799)
        pitch=12.7;teeth=20;diameter=pitch/math.sin(math.pi/teeth)
        self.assertAlmostEqual(diameter*math.sin(math.pi/teeth),pitch)
        self.assertAlmostEqual(12.7*20*60/60000,.254)
        self.assertAlmostEqual(10**6*8/(60*1000),133.333333333333)
        self.assertAlmostEqual(4/math.sqrt(2),2.82842712474619)
        self.assertAlmostEqual(math.sqrt(3)*10**2/2,86.6025403784439)


if __name__=='__main__':unittest.main()
