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

    def test_pressed_wing_native_rows_keep_the_historical_family_separate(self):
        pages={p['path']:p for p in self.data['pages']}
        page=pages['05-NUTS/09-WING/01-PRESSED']
        # Independently transcribed STN excerpt p. 16: d1, D, e, g1, g2,
        # overall h, hub p, wing R1, hub R2 (archived R5).
        expected={
            'M3':(4.4,6.2,15,2.2,2.7,7.7,2.4,4,4.8),
            'M4':(5.8,8.3,20,3,4,10.5,3.2,6,6.5),
            'M5':(6.7,10.4,25,3.5,4.7,13.5,4,7,8.1),
            'M6':(8.7,12.4,31,4,5.5,16,4.8,8,9.7),
            'M8':(10.6,16.6,40,5,8,20,6.4,11,10.1),
            'M10':(12.7,20.7,51,6,9.5,25.5,8,15,12.1)}
        self.assertEqual({r[0]:tuple(float(x) for x in r[2:]) for r in page['rows']},expected)
        self.assertEqual(pages['05-NUTS/09-WING']['source_table'],'05-NUTS/09-WING#0')
        self.assertEqual(page['sources'],['pressed_wing_stn'])
        self.assertEqual(set(page['figures'][0]['localized_paths']),set(generator.LANGUAGES))
        for language in generator.LANGUAGES:
            self.assertIn('01-PRESSED/0000-index/index_'+language+'.html',generator.render(self.data,pages['05-NUTS/09-WING'],language))

    def test_reviewed_weld_families_replace_only_the_visible_historical_offer(self):
        pages={p['path']:p for p in self.data['pages']}
        self.assertEqual(pages['05-NUTS/05-WELD']['rows'],[])
        retired=[p for p in self.data['retired_pages'] if p['path']=='05-NUTS/05-WELD']
        self.assertEqual(len(retired),1)
        self.assertEqual(retired[0]['rows'],self.data['curated_source_tables']['05-NUTS/05-WELD#0']['rows'])
        for family,count,flats,height in [('01-HEXAGON',16,14,6.5),('02-FLANGE',14,13,10)]:
            page=pages['05-NUTS/05-WELD/'+family]
            block=generator.screen_table_blocks(page)[0]
            self.assertEqual(len(block['rows']),count)
            representative=next(row for row in block['rows'] if self.data['labels'][row[0]['key']][1]=='M8×1.25')
            self.assertEqual([float(v) for v in representative[1:4]],[1.25,flats,height])
            self.assertTrue(page['localized_illustrations'])
            self.assertEqual(set(page['figures'][0]['localized_paths']),set(generator.LANGUAGES))
            self.assertIn('weld_contact',page['notes'])

    def test_din_cap_nuts_are_separate_and_closed(self):
        from lxml import html
        pages={p['path']:p for p in self.data['pages']}
        page=pages['05-NUTS/04-CAP/02-DIN1587']
        self.assertEqual(pages['05-NUTS/04-CAP']['rows'],[])
        retired=next(p for p in self.data['retired_pages'] if p['path']=='05-NUTS/04-CAP')
        self.assertEqual(retired['source_table'],'05-NUTS/04-CAP#0')
        self.assertEqual(len(page['rows']),26)
        rows={row[0]:row for row in page['rows']}
        self.assertEqual(rows['M10×1.5'][2:5],['16','18','8'])
        self.assertEqual(rows['M22×2.5'][2],'34')
        self.assertIn('M10×1',rows)
        self.assertNotIn('M6×0.75',rows)
        for row in page['rows']:
            pitch,h,t,p,minimum,wall=[float(row[i]) for i in (1,3,7,8,9,10)]
            self.assertGreaterEqual(wall,minimum)
            self.assertGreater(p,t+.5*pitch)
            self.assertAlmostEqual(wall,h-p,places=5)
        self.assertTrue(page['freeze_header'])
        for language in self.data['languages']:
            tree=html.fromstring(generator.render(self.data,page,language))
            self.assertEqual(len(tree.xpath('//div[@class="screen-only"]//table/tbody/tr')),26)
            self.assertIn('CAD',' '.join(tree.xpath('//thead//text()')))
            self.assertTrue(all(key in self.data['labels'] for key in page['notes']))

    def test_csn_closed_nuts_use_primary_cone_and_flat_dimensions(self):
        from lxml import html
        pages={p['path']:p for p in self.data['pages']}
        page=pages['05-NUTS/04-CAP/01-CSN021431']
        self.assertEqual(len(page['rows']),11)
        rows={r[0]:r for r in page['rows']}
        self.assertEqual(rows['M8×1.25'][2:7],['13','14.38','16.5','8','10.5'])
        self.assertEqual(rows['M10×1.5'][2],'17')
        self.assertEqual(rows['M12×1.75'][2],'19')
        self.assertEqual(rows['M14×2'][2],'22')
        self.assertEqual(rows['M30×3.5'][-1],'54.5')
        for row in page['rows']:
            self.assertLess(float(row[-1]),float(row[4]))
            self.assertLess(float(row[-2]),float(row[-1]))
        self.assertEqual(self.data['sources'][page['sources'][0]]['url'],'https://nahledy.normy.biz/n.php?i=254')
        self.assertEqual(set(page['figures'][0]['localized_paths']),set(generator.LANGUAGES))
        for language in generator.LANGUAGES:
            tree=html.fromstring(generator.render(self.data,page,language))
            self.assertEqual(len(tree.xpath('//div[@class="screen-only"]//table/tbody/tr')),11)
            text=' '.join(tree.xpath('//text()'))
            self.assertIn('45°',text);self.assertIn('120°',text)

    def test_coupling_nuts_retain_trade_scope_and_3d_dimensions(self):
        from lxml import html
        page=next(p for p in self.data['pages'] if p['path']=='05-NUTS/11-COUPLING')
        self.assertEqual(len(page['rows']),14)
        for row in page['rows']:
            self.assertEqual(float(row[3]),3*float(row[0][1:]))
            self.assertGreaterEqual(2*float(row[2])/math.sqrt(3),float(row[4]))
        self.assertEqual(next(r for r in page['rows'] if r[0]=='M12')[1:4],['1.75','19','36'])
        self.assertEqual(set(page['figures'][0]['localized_paths']),set(generator.LANGUAGES))
        for lang in generator.LANGUAGES:
            tree=html.fromstring(generator.render(self.data,page,lang))
            self.assertEqual(len(tree.xpath('//div[contains(@class,"table-scroll")]/table/tbody/tr')),14)
            self.assertIn('DIN 6334',' '.join(tree.xpath('//text()')))
        self.assertIn('trade designation',self.data['labels']['coupling_scope'][1])

    def test_shaped_families_keep_reviewed_types_angles_and_compressed_state(self):
        import configparser
        from lxml import html
        pages={p['path']:p for p in self.data['pages']}
        families=[('05-NUTS/10-FLANGE',8),('05-NUTS/12-WHEEL/01-SPHERICAL',1),
            ('05-NUTS/12-WHEEL/02-CONICAL',2),('06-WASHERS/07-SPHERICAL',1)]
        for path,count in families:
            page=pages[path];self.assertEqual(len(page['rows']),count)
            folder=ROOT/self.data['root']/path;models=list(folder.glob('*.prtz'));self.assertEqual(len(models),count)
            for model in models:
                doc=configparser.RawConfigParser();doc.read(model,encoding='utf-8')
                values=json.loads(doc['UserParameterValues']['Data'])
                self.assertGreater(float(values['mass']['']),0)
                self.assertEqual(set(values['name']),set(generator.LANGUAGES))
            for lang in generator.LANGUAGES:
                tree=html.fromstring(generator.render(self.data,page,lang))
                tables=tree.xpath('//div[contains(@class,"table-scroll")]/table')
                self.assertTrue(tables)
                self.assertTrue(all(len(t.xpath('./tbody/tr'))==count for t in tables))
        flange=pages[families[0][0]]['rows'];self.assertEqual(next(r for r in flange if r[0]=='M8')[1:5],['1.25','13','8','17.9'])
        spherical=pages[families[1][0]]['rows'][0];self.assertEqual(spherical[:4],['M20×1.5','1.5','27','27'])
        conical=pages[families[2][0]];self.assertEqual({r[-1] for r in conical['rows']},{'60','90'});self.assertEqual(len(conical['figures']),2)
        washer=pages[families[3][0]];self.assertEqual(washer['rows'],[['20.5','34','24','8','18']])
        self.assertIn('compressed',self.data['labels']['shaped_washer_scope'][1])
        self.assertEqual(pages['05-NUTS/12-WHEEL']['rows'],[])

    def test_reviewed_special_families_and_native_washer_art(self):
        pages={p['path']:p for p in self.data['pages']}
        reviewed=json.loads((ROOT/'tools/data/special-cap-screw-reviewed-dimensions.json').read_text(encoding='utf-8'))
        for family in reviewed['families']:
            page=pages['04-SCREWS/04-CAP-HEAD/'+family['directory']]
            self.assertEqual(page['review'],'reviewed_catalogue')
            self.assertEqual(len(page['sections'][0]['rows']),len(family['sizes']))
            self.assertTrue(page['freeze_header'])
            self.assertIn('catalogue_range_note',page['notes'])
            self.assertTrue(all(len(self.data['labels'][key])==5 for key in page['notes']))
            availability=page['sections'][1]
            self.assertEqual(len(availability['headers']),len(family['sizes'])+1)
            for row in availability['rows']:
                length=float(row[0])
                self.assertEqual(row[1:],['✓' if length in s['catalogue_lengths'] else '—' for s in family['sizes']])
        for topic,count in [('01-HEXAGON',3),('02-CAP-HEAD',1),('03-SPRING',2)]:
            page=pages['06-WASHERS/'+topic]
            self.assertTrue(page['localized_illustrations'])
            self.assertEqual(len(page['figures']),count)
            for figure in page['figures']:
                self.assertEqual(set(figure['localized_paths']),set(generator.LANGUAGES))
                folder=ROOT/self.data['root']/page['path']/'0000-index'
                for image in [figure['path'],*figure['localized_paths'].values()]:
                    self.assertTrue((folder/image).is_file(),image)
                self.assertTrue((folder/Path(figure['path']).with_suffix('.prtz')).is_file())
                self.assertTrue((folder/Path(figure['path']).with_suffix('.drwz')).is_file())

    def test_reviewed_grade_c_nuts_have_feasible_dimensions(self):
        table=self.data['curated_source_tables']['05-NUTS/01-HEXAGON/08-GRADE-C#0']
        number=lambda value:float(value.replace(',','.'))
        for col,header in enumerate(table['headers'][1:],1):
            diameter=number(header.removeprefix('symbol_M'))
            corners,maximum,minimum,flats=[number(row[col]) for row in table['rows']]
            self.assertGreater(flats,diameter,header)
            self.assertGreaterEqual(maximum,minimum,header)
            self.assertGreaterEqual(2*flats/math.sqrt(3),corners,header)
        reviewed=json.loads((ROOT/'tools/data/nut-reviewed-dimensions.json').read_text(encoding='utf-8'))
        self.assertEqual(len(reviewed['corrections']),10)
        self.assertEqual(table['rows'][3][2],'10')
        self.assertEqual(table['rows'][2][2],'4,9')
        self.assertEqual(table['rows'][3][10],'34')

    def test_iso_washer_ranges_dimensions_and_historical_separation(self):
        reviewed=json.loads((ROOT/'tools/data/plain-washer-iso-reviewed-dimensions.json').read_text(encoding='utf-8'))
        counts={'ISO 7089':29,'ISO 7090':23,'ISO 7091':29,'ISO 7092':21}
        pages={p['path']:p for p in self.data['pages']}
        for family in reviewed['families']:
            standard=family['standard'];sizes=family['sizes']
            self.assertEqual(len(sizes),counts[standard])
            self.assertEqual(len({s['nominal_mm'] for s in sizes}),len(sizes))
            self.assertEqual([min(s['nominal_mm'] for s in sizes),max(s['nominal_mm'] for s in sizes)],family['nominal_range_mm'])
            page=pages['06-WASHERS/'+('02-CAP-HEAD' if standard=='ISO 7092' else '01-HEXAGON')]
            block=next(b for b in page['sections'] if b.get('iso_washer_standard')==standard)
            self.assertEqual([float(r[0]) for r in block['rows']],[s['nominal_mm'] for s in sizes])
            for size,row in zip(sizes,block['rows']):
                keys=('nominal_mm','bore_mm','bore_max_mm','outside_mm','outside_min_mm','thickness_mm','thickness_max_mm','thickness_min_mm')
                self.assertEqual([float(v) for v in row],[size[k] for k in keys])
                self.assertLess(size['bore_mm'],size['outside_mm'])
                self.assertLessEqual(size['thickness_min_mm'],size['thickness_mm'])
                self.assertLessEqual(size['thickness_mm'],size['thickness_max_mm'])
            if standard=='ISO 7090':self.assertTrue(.25<=family['chamfer_depth_fraction']<=.5)
        historic=next(b for b in pages['06-WASHERS/01-HEXAGON']['sections'] if b.get('heading')=='historical_washer_outside_iso')
        self.assertEqual(len(historic['rows']),32)
        self.assertTrue(all(float(r[2])>64 and r[0].startswith('ČSN') for r in historic['rows']))
        # These differences demonstrate why a label-only CSN conversion is wrong.
        normal=next(f for f in reviewed['families'] if f['standard']=='ISO 7089')
        self.assertEqual(next(s for s in normal['sizes'] if s['nominal_mm']==5)['thickness_mm'],1)
        self.assertEqual(next(s for s in normal['sizes'] if s['nominal_mm']==64)['bore_mm'],70)

    def test_all_split_cells_reassemble_without_loss(self):
        restored = {}
        for page in (block for topic in self.data['pages']+self.data.get('retired_pages',[]) for block in generator.table_blocks(topic)):
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
                    'https://cdn.standards.iteh.ai/samples/54039/15c540b697434b5d85128ca34672939c/ISO-1207-2011.pdf',
                    'https://encyklopediapoznania.sk/data/eknihy/strojarenstvo/strojnicke_tabulky_z_roku_1965.pdf',
                    'https://cdn.standards.iteh.ai/samples/61670/f88b1947eed6421593d79302b800ce67/ISO-4034-2012.pdf',
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
            # Qt may add empty separator lines when saving user-edited metadata.
            # Preserve every authored field while ignoring those INI separators.
            authored = (folder / 'metadata.ini').read_text(encoding='utf-8')
            self.assertEqual([line for line in authored.splitlines() if line.strip()],
                             [line for line in generator.metadata(self.data, page).splitlines() if line.strip()])
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
        self.assertEqual(len(pages),28)
        for page in pages:
            blocks=generator.screen_table_blocks(page)
            combined=next(block for block in blocks if block['headers'][0]=='symbol_d')
            self.assertEqual(combined['headers'][0],'symbol_d')
            self.assertEqual(sum(len(block['rows']) for block in blocks),page['display_row_count'])
            for source in generator.presentation_table_blocks(page):
                if source['headers'][0]!='parameter':
                    self.assertIn(source,blocks)
                    continue
                positions=[];occurrences={}
                for row in source['rows']:
                    display=source.get('parameter_labels',{}).get(json.dumps(row[0],sort_keys=True),row[0])
                    name=display if isinstance(display,dict) else {'literal':display}
                    candidates=[i for i,header in enumerate(combined['headers']) if header==name]
                    token=json.dumps(name,sort_keys=True)
                    occurrence=occurrences.get(token,0);occurrences[token]=occurrence+1
                    positions.append(candidates[occurrence])
                for column,thread in enumerate(source['headers'][1:],1):
                    expected=[row[column] for row in source['rows']]
                    self.assertTrue(any(row[0]=={'key':thread} and [row[i] for i in positions]==expected for row in combined['rows']), (page['path'],thread))

    def test_screw_directory_names_are_descriptive_without_standard_numbers(self):
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        for page in data['pages']:
            if not page['path'].startswith('04-SCREWS/'):continue
            for label in data['labels'][page['title']]:
                self.assertNotRegex(label,r'\b(?:ISO|DIN|ČSN)\s*\d')
            self.assertTrue(all(data['labels'][page['title']]))

    def test_every_nut_dimension_page_uses_thread_rows_and_frozen_header(self):
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        checked=0
        for page in data['pages']:
            if not page['path'].startswith('05-NUTS/'):continue
            dimensions=[b for b in generator.presentation_table_blocks(page) if b['headers'][0]=='parameter']
            if not dimensions:continue
            self.assertTrue(page['thread_dimension_rows'],page['path'])
            self.assertTrue(page['freeze_header'],page['path'])
            displayed=[b for b in generator.screen_table_blocks(page) if b['headers'][0]=='symbol_d']
            self.assertEqual(len(displayed),1,page['path'])
            checked+=1
        self.assertEqual(checked,17)

    def test_castle_dimensions_form_one_table_without_duplicate_cotter_columns(self):
        from lxml import html
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        pages=[p for p in data['pages'] if p['path'].startswith('05-NUTS/02-CASTLE/')]
        self.assertEqual(len(pages),2)
        for page in pages:
            blocks=generator.screen_table_blocks(page)
            self.assertEqual(len(blocks),1)
            self.assertEqual(len(blocks[0]['rows']),16)
            self.assertEqual(blocks[0]['headers'].count({'key':'castle_cotter_d'}),1)
            self.assertEqual(blocks[0]['headers'].count({'key':'castle_cotter_l'}),1)
            self.assertEqual(blocks[0]['headers'].count({'key':'castle_fine_thread'}),1)
            for lang in data['languages']:
                doc=html.fromstring(generator.render(data,page,lang))
                tables=doc.xpath('//div[contains(@class,"table-scroll--frozen")]/table')
                self.assertEqual(len(tables),1)
                self.assertEqual(len(tables[0].xpath('./tbody/tr')),16)

    def test_stud_availability_matches_every_source_cell(self):
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        page=next(p for p in data['pages'] if p['path']=='04-SCREWS/03-STUDS')
        blocks=generator.screen_table_blocks(page)
        for source in generator.table_blocks(page):
            if not source.get('stud_length_availability'):continue
            actual=next(b for b in blocks if b.get('source_table')==source['source_table'])
            expected=[r for r in source['rows'] if isinstance(r[0],str) and r[0].isdigit()]
            self.assertEqual([r[0] for r in actual['rows']],[r[0] for r in expected])
            for before,after in zip(expected,actual['rows']):
                self.assertEqual(after[1:],['—' if c in ('','—','–','-') else '✓' for c in before[1:]])
        first=next(b for b in blocks if b.get('stud_length_availability'))
        self.assertEqual([int(r[0]) for r in first['rows'] if r[1]=='✓'],[12,14,16,18,20,22,25,28])
        for language in data['languages']:
            text=generator.render(data,page,language)
            self.assertIn('✓',text)
            self.assertNotIn(data['labels']['mass_Přibližná hmotnost šroubu podle ČSN 01 1174 [g]'][data['languages'].index(language)],text)

    def test_approximate_mass_sections_are_not_published(self):
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        for page in data['pages']:
            for block in generator.presentation_table_blocks(page):
                self.assertFalse(block.get('approximate_mass_only'))
                self.assertFalse(any(isinstance(c,dict) and c.get('key','').startswith('mass_') for row in block['rows'] for c in row))
        socket=next(p for p in data['pages'] if p['path']=='04-SCREWS/04-CAP-HEAD/01-SOCKET')
        for language in data['languages']:
            text=generator.render(data,socket,language)
            self.assertNotIn('kg/1000',text)
            self.assertIn('+ / +',text)

    def test_washer_mass_columns_are_omitted_in_every_language(self):
        from lxml import html
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        for page in data['pages']:
            if not page['path'].startswith('06-WASHERS/') or not page.get('approximate_mass_columns'):continue
            original_rows=json.dumps(page['rows'],ensure_ascii=False)
            for language in data['languages']:
                document=html.fromstring(generator.render(data,page,language))
                headings=[' '.join(node.itertext()) for node in document.xpath('//thead//th')]
                for column in page['approximate_mass_columns']:
                    label=data['labels'][page['headers'][column]][data['languages'].index(language)]
                    self.assertNotIn(label,headings)
                for block in generator.presentation_table_blocks(page):
                    self.assertTrue(all(len(row)==len(block['headers']) for row in block['rows']))
            self.assertEqual(json.dumps(page['rows'],ensure_ascii=False),original_rows)

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
        self.assertEqual(page['figures'][0]['path'],'counterbores-horizontal.svg')
        self.assertTrue((ROOT/data['root']/page['path']/'0000-index/01.png').is_file())
        import base64
        import xml.etree.ElementTree as ET
        folder=ROOT/data['root']/page['path']/'0000-index'
        composite=ET.parse(folder/page['figures'][0]['path']).getroot()
        embedded=composite.find('.//{http://www.w3.org/2000/svg}image')
        self.assertEqual(base64.b64decode(embedded.attrib['href'].split(',',1)[1]),(folder/'01.png').read_bytes())
        figures=ROOT/'config/lib/03-ENGINEERING-TABLES/0000-index/diagrams'
        image=ET.parse(figures/'hex-screw-reference.svg').getroot().find('.//{http://www.w3.org/2000/svg}image')
        self.assertTrue(image.attrib['href'].startswith('data:image/png;base64,'))
        self.assertEqual(base64.b64decode(image.attrib['href'].split(',',1)[1]),(figures/'hex-screw-native.png').read_bytes())


if __name__ == '__main__':
    unittest.main()
