"""Finite factory naming, representative selection and localized artwork contracts."""
import importlib.util
from pathlib import Path
import unittest
import json

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('manifest',ROOT/'tools/prepare-fastener-manifest.py')
manifest=importlib.util.module_from_spec(spec);spec.loader.exec_module(manifest)


class FastenerManifest(unittest.TestCase):
    def test_one_length_per_standard_diameter_and_pitch(self):
        variants=manifest.prepare()['variants']
        self.assertEqual(len(variants),200)
        self.assertEqual(len({(v['standard'],v['diameter_mm'],v['pitch_mm']) for v in variants}),200)
        self.assertEqual(len({v['filename'].casefold() for v in variants}),200)
        for standard in {v['standard'] for v in variants}:
            family=[v for v in variants if v['standard']==standard]
            ordered=sorted(family,key=lambda v:v['filename'])
            self.assertEqual([v['diameter_mm'] for v in ordered],sorted(v['diameter_mm'] for v in family))
            # Coarse names omit pitch; compare pitches within explicitly named
            # fine-thread files, rather than inventing a numeric coarse token.
            fine=[v for v in ordered if v['fine_pitch']]
            self.assertEqual([(v['diameter_mm'],v['pitch_mm']) for v in fine],
                sorted((v['diameter_mm'],v['pitch_mm']) for v in fine))

    def test_padding_changes_only_filenames(self):
        for variant in manifest.prepare()['variants']:
            self.assertNotRegex(variant['designation'],r'M0')
            self.assertNotRegex(variant['filename'],r'-[ABC]\.prtz$')
            self.assertEqual(variant['filename'],manifest.library_filename(variant))
            if variant['product_grade']=='C':self.assertGreaterEqual(variant['diameter_mm'],5)

    def test_integer_fractional_pitch_and_length_order(self):
        variants=[dict(standard='ISO 4014',full_thread=False,fine_pitch=True,diameter_mm=d,pitch_mm=p,length_mm=l)
            for d in (1.6,2,2.5,3,3.5,6,8,10,12,60,64)
            for p in (1,1.25,1.5,2,2.5) for l in (8,20,80,100,500)]
        self.assertEqual(sorted(variants,key=manifest.library_filename),variants)

    def test_localized_figures_and_neutral_fallback(self):
        spec=importlib.util.spec_from_file_location('references',ROOT/'tools/generate-reference-tables.py')
        generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
        data=generator.load_data(ROOT/'tools/data/fastener-tables.json')
        pages=[p for p in data['pages'] if p.get('localized_illustrations')]
        self.assertEqual(len(pages),7)
        from lxml import html
        for page in pages:
            folder=ROOT/data['root']/page['path']/'0000-index'
            for language in data['languages']:
                document=html.fromstring(generator.render(data,page,language,True))
                figures=document.xpath('//figure/img/@src')
                self.assertEqual(figures,[f['localized_paths'][language] for f in page['figures']])
                self.assertTrue(all((folder/f).is_file() for f in figures))
                self.assertFalse(document.xpath('//table'))
                self.assertEqual((folder/f'01_{language}.html').read_text(encoding='utf-8'),generator.render(data,page,language,True))
            self.assertEqual((folder/'01.html').read_text(encoding='utf-8'),generator.render(data,page,'en',True))
        copy=json.loads(json.dumps(data));page=next(p for p in copy['pages'] if p.get('localized_illustrations'))
        page['figures'][0]['localized_paths']['ru']='missing-optional-ru.png'
        document=html.fromstring(generator.render(copy,page,'ru',True))
        self.assertEqual(document.xpath('//figure/img/@src')[0],page['figures'][0]['path'])


if __name__=='__main__':unittest.main()
